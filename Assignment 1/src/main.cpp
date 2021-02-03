//Just a simple handler for simple initialization stuffs
#include "Utilities/BackendHandler.h"

#include <filesystem>
#include <json.hpp>
#include <fstream>

#include <Texture2D.h>
#include <Texture2DData.h>
#include <MeshBuilder.h>
#include <MeshFactory.h>
#include <NotObjLoader.h>
#include <ObjLoader.h>
#include <VertexTypes.h>
#include <ShaderMaterial.h>
#include <RendererComponent.h>
#include <TextureCubeMap.h>
#include <TextureCubeMapData.h>

#include <Timing.h>
#include <GameObjectTag.h>
#include <InputHelpers.h>

#include <IBehaviour.h>
#include <CameraControlBehaviour.h>
#include <FollowPathBehaviour.h>
#include <SimpleMoveBehaviour.h>

bool lighton;

int main() {
	int frameIx = 0;
	float fpsBuffer[128];
	float minFps, maxFps, avgFps;
	int selectedVao = 0; // select cube by default
	std::vector<GameObject> controllables;

	BackendHandler::InitAll();

	// Let OpenGL know that we want debug output, and route it to our handler function
	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(BackendHandler::GlDebugMessage, nullptr);

	// Enable texturing
	glEnable(GL_TEXTURE_2D);

	// Push another scope so most memory should be freed *before* we exit the app
	{
		#pragma region Shader and ImGui
		Shader::sptr passthroughShader = Shader::Create();
		passthroughShader->LoadShaderPartFromFile("shaders/passthrough_vert.glsl", GL_VERTEX_SHADER);
		passthroughShader->LoadShaderPartFromFile("shaders/passthrough_frag.glsl", GL_FRAGMENT_SHADER);
		passthroughShader->Link();


		// Load our shaders
		Shader::sptr shader = Shader::Create();
		shader->LoadShaderPartFromFile("shaders/vertex_shader.glsl", GL_VERTEX_SHADER);
		shader->LoadShaderPartFromFile("shaders/frag_phong.glsl", GL_FRAGMENT_SHADER);
		shader->Link();

		glm::vec3 lightPos = glm::vec3(0.0f, 0.0f, 5.0f);
		glm::vec3 lightCol = glm::vec3(0.5f, 0.5f, 0.7f);
		float     lightAmbientPow = 2.0f;
		float     lightSpecularPow = 1.0f;
		glm::vec3 ambientCol = glm::vec3(1.0f);
		float     ambientPow = 0.1f;
		float     lightLinearFalloff = 0.09f;
		float     lightQuadraticFalloff = 0.032f;
		float	  diffuse = 1.0f;


		// These are our application / scene level uniforms that don't necessarily update
		// every frame
		shader->SetUniform("u_LightPos", lightPos);
		shader->SetUniform("u_LightCol", lightCol);
		shader->SetUniform("u_AmbientLightStrength", lightAmbientPow);
		shader->SetUniform("u_SpecularLightStrength", lightSpecularPow);
		shader->SetUniform("u_AmbientCol", ambientCol);
		shader->SetUniform("u_AmbientStrength", ambientPow);
		shader->SetUniform("u_LightAttenuationConstant", 1.0f);
		shader->SetUniform("u_LightAttenuationLinear", lightLinearFalloff);
		shader->SetUniform("u_LightAttenuationQuadratic", lightQuadraticFalloff);
		shader->SetUniform("u_toonShading", diffuse);

		// We'll add some ImGui controls to control our shader
		BackendHandler::imGuiCallbacks.push_back([&]() {
			if (ImGui::CollapsingHeader("Environment generation"))
			{
				if (ImGui::Button("Regenerate Environment", ImVec2(200.0f, 40.0f)))
				{
					EnvironmentGenerator::RegenerateEnvironment();
				}
			}
			if (ImGui::CollapsingHeader("Scene Level Lighting Settings"))
			{
				if (ImGui::ColorPicker3("Ambient Color", glm::value_ptr(ambientCol))) {
					shader->SetUniform("u_AmbientCol", ambientCol);
				}
				if (ImGui::SliderFloat("Fixed Ambient Power", &ambientPow, 0.01f, 1.0f)) {
					shader->SetUniform("u_AmbientStrength", ambientPow);
				}
			}
			if (ImGui::CollapsingHeader("Light Level Lighting Settings"))
			{
				if (ImGui::DragFloat3("Light Pos", glm::value_ptr(lightPos), 0.01f, -10.0f, 10.0f)) {
					shader->SetUniform("u_LightPos", lightPos);
				}
				if (ImGui::ColorPicker3("Light Col", glm::value_ptr(lightCol))) {
					shader->SetUniform("u_LightCol", lightCol);
				}
				if (ImGui::SliderFloat("Light Ambient Power", &lightAmbientPow, 0.0f, 1.0f)) {
					shader->SetUniform("u_AmbientLightStrength", lightAmbientPow);
				}
				if (ImGui::SliderFloat("Light Specular Power", &lightSpecularPow, 0.0f, 1.0f)) {
					shader->SetUniform("u_SpecularLightStrength", lightSpecularPow);
				}
				if (ImGui::DragFloat("Light Linear Falloff", &lightLinearFalloff, 0.01f, 0.0f, 1.0f)) {
					shader->SetUniform("u_LightAttenuationLinear", lightLinearFalloff);
				}
				if (ImGui::DragFloat("Light Quadratic Falloff", &lightQuadraticFalloff, 0.01f, 0.0f, 1.0f)) {
					shader->SetUniform("u_LightAttenuationQuadratic", lightQuadraticFalloff);
				}
			}

			auto name = controllables[selectedVao].get<GameObjectTag>().Name;
			ImGui::Text(name.c_str());
			auto behaviour = BehaviourBinding::Get<SimpleMoveBehaviour>(controllables[selectedVao]);
			ImGui::Checkbox("Relative Rotation", &behaviour->Relative);

			ImGui::Text("Q/E -> Yaw\nLeft/Right -> Roll\nUp/Down -> Pitch\nY -> Toggle Mode");
		
			minFps = FLT_MAX;
			maxFps = 0;
			avgFps = 0;
			for (int ix = 0; ix < 128; ix++) {
				if (fpsBuffer[ix] < minFps) { minFps = fpsBuffer[ix]; }
				if (fpsBuffer[ix] > maxFps) { maxFps = fpsBuffer[ix]; }
				avgFps += fpsBuffer[ix];
			}
			ImGui::PlotLines("FPS", fpsBuffer, 128);
			ImGui::Text("MIN: %f MAX: %f AVG: %f", minFps, maxFps, avgFps / 128.0f);
			});

		#pragma endregion 

		// GL states
		glEnable(GL_DEPTH_TEST);
		glEnable(GL_CULL_FACE);
		glDepthFunc(GL_LEQUAL); // New 

		#pragma region TEXTURE LOADING

		// Load some textures from files
		Texture2D::sptr stone = Texture2D::LoadFromFile("images/stone.jpg");
		Texture2D::sptr stoneBump = Texture2D::LoadFromFile("images/stone_bump.jpg");
		Texture2D::sptr stoneSpec = Texture2D::LoadFromFile("images/Stone_001_Specular.png");
		Texture2D::sptr grass = Texture2D::LoadFromFile("images/grass.jpg");
		Texture2D::sptr noSpec = Texture2D::LoadFromFile("images/grassSpec.png");
		Texture2D::sptr box = Texture2D::LoadFromFile("images/box.bmp");
		Texture2D::sptr boxSpec = Texture2D::LoadFromFile("images/box-reflections.bmp");
		Texture2D::sptr simpleFlora = Texture2D::LoadFromFile("images/SimpleFlora.png");
		Texture2D::sptr snowSpec = Texture2D::LoadFromFile("images/snow.jpg");
		Texture2D::sptr snowSpec_spec = Texture2D::LoadFromFile("images/snow_spec.jpg");
		Texture2D::sptr flowerSpec = Texture2D::LoadFromFile("images/flower_texture.png");
		Texture2D::sptr mooshSpec = Texture2D::LoadFromFile("images/mushroom_texture.png");
		Texture2D::sptr grassLeafSpec = Texture2D::LoadFromFile("images/grass_leaf.png");
		Texture2D::sptr bushSpec = Texture2D::LoadFromFile("images/bush.png");


		// Load the cube map
		//TextureCubeMap::sptr environmentMap = TextureCubeMap::LoadFromImages("images/cubemaps/skybox/sample.jpg");
		TextureCubeMap::sptr environmentMap = TextureCubeMap::LoadFromImages("images/cubemaps/skybox/ToonSky.jpg"); 

		// Creating an empty texture
		Texture2DDescription desc = Texture2DDescription();  
		desc.Width = 1;
		desc.Height = 1;
		desc.Format = InternalFormat::RGB8;
		Texture2D::sptr texture2 = Texture2D::Create(desc);
		// Clear it with a white colour
		texture2->Clear();

		#pragma endregion

		///////////////////////////////////// Scene Generation //////////////////////////////////////////////////
		#pragma region Scene Generation
		
		// We need to tell our scene system what extra component types we want to support
		GameScene::RegisterComponentType<RendererComponent>();
		GameScene::RegisterComponentType<BehaviourBinding>();
		GameScene::RegisterComponentType<Camera>();

		// Create a scene, and set it to be the active scene in the application
		GameScene::sptr scene = GameScene::Create("test");
		Application::Instance().ActiveScene = scene;

		// We can create a group ahead of time to make iterating on the group faster
		entt::basic_group<entt::entity, entt::exclude_t<>, entt::get_t<Transform>, RendererComponent> renderGroup =
			scene->Registry().group<RendererComponent>(entt::get_t<Transform>());

		// Create a material and set some properties for it
		ShaderMaterial::sptr stoneMat = ShaderMaterial::Create();  
		stoneMat->Shader = shader;
		stoneMat->Set("s_Diffuse", stone);
		stoneMat->Set("s_Specular", stoneBump);
		stoneMat->Set("u_Shininess", 2.0f);
		stoneMat->Set("u_TextureMix", 0.0f); 

		ShaderMaterial::sptr grassMat = ShaderMaterial::Create();
		grassMat->Shader = shader;
		grassMat->Set("s_Diffuse", grass);
		grassMat->Set("s_Specular", noSpec);
		grassMat->Set("u_Shininess", 2.0f);
		grassMat->Set("u_TextureMix", 0.0f);

		ShaderMaterial::sptr boxMat = ShaderMaterial::Create();
		boxMat->Shader = shader;
		boxMat->Set("s_Diffuse", box);
		boxMat->Set("s_Specular", boxSpec);
		boxMat->Set("u_Shininess", 8.0f);
		boxMat->Set("u_TextureMix", 0.0f);

		ShaderMaterial::sptr simpleFloraMat = ShaderMaterial::Create();
		simpleFloraMat->Shader = shader;
		simpleFloraMat->Set("s_Diffuse", simpleFlora);
		simpleFloraMat->Set("s_Specular", noSpec);
		simpleFloraMat->Set("u_Shininess", 8.0f);
		simpleFloraMat->Set("u_TextureMix", 0.0f);

		ShaderMaterial::sptr snowMat = ShaderMaterial::Create();
		snowMat->Shader = shader;
		snowMat->Set("s_Diffuse", snowSpec);
		snowMat->Set("s_Specular", snowSpec_spec);
		snowMat->Set("u_Shininess", 1.0f);
		snowMat->Set("u_TextureMix", 0.0f);

		ShaderMaterial::sptr flowerMat = ShaderMaterial::Create();
		flowerMat->Shader = shader;
		flowerMat->Set("s_Diffuse", flowerSpec);
		flowerMat->Set("s_Specular", noSpec);
		flowerMat->Set("u_Shininess", 1.0f);
		flowerMat->Set("u_TextureMix", 0.0f);

		ShaderMaterial::sptr mooshMat = ShaderMaterial::Create();
		mooshMat->Shader = shader;
		mooshMat->Set("s_Diffuse", mooshSpec);
		mooshMat->Set("s_Specular", noSpec);
		mooshMat->Set("u_Shininess", 1.0f);
		mooshMat->Set("u_TextureMix", 0.0f);

		ShaderMaterial::sptr grassleafMat = ShaderMaterial::Create();
		grassleafMat->Shader = shader;
		grassleafMat->Set("s_Diffuse", grassLeafSpec);
		grassleafMat->Set("s_Specular", noSpec);
		grassleafMat->Set("u_Shininess", 1.0f);
		grassleafMat->Set("u_TextureMix", 0.0f);

		ShaderMaterial::sptr bushMat = ShaderMaterial::Create();
		bushMat->Shader = shader;
		bushMat->Set("s_Diffuse", bushSpec);
		bushMat->Set("s_Specular", noSpec);
		bushMat->Set("u_Shininess", 1.0f);
		bushMat->Set("u_TextureMix", 0.0f);

		GameObject obj1 = scene->CreateEntity("Ground"); 
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/plane.obj");
			obj1.emplace<RendererComponent>().SetMesh(vao).SetMaterial(grassMat);
			obj1.get<Transform>().SetLocalPosition(0.0f, 0.0f, 0.0f);
		}

		GameObject obj2 = scene->CreateEntity("tombstone");
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/tombstone.obj");
			obj2.emplace<RendererComponent>().SetMesh(vao).SetMaterial(stoneMat);
			obj2.get<Transform>().SetLocalPosition(0.0f, 0.0f, 0.0f);
			obj2.get<Transform>().SetLocalRotation(90.0f, 0.0f, -90.0f);
			BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(obj2);
		}

		GameObject obj3 = scene->CreateEntity("arm");
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/Hand_L.obj");
			obj3.emplace<RendererComponent>().SetMesh(vao).SetMaterial(snowMat);
			obj3.get<Transform>().SetLocalPosition(0.0f, 0.0f, -0.5f);
			obj3.get<Transform>().SetLocalRotation(180.0f, 0.0f, 30.0f);
			obj3.get<Transform>().SetLocalScale(glm::vec3(3.0f));
			//BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(obj3);
		}

		GameObject obj4 = scene->CreateEntity("rib");
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/ribs.obj");
			obj4.emplace<RendererComponent>().SetMesh(vao).SetMaterial(snowMat);
			obj4.get<Transform>().SetLocalPosition(-5.0f, 15.0f, -0.5f);
			obj4.get<Transform>().SetLocalRotation(180.0f, -20.0f, 30.0f);
			obj4.get<Transform>().SetLocalScale(glm::vec3(2.0f));
			//BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(obj3);
		}

		GameObject obj5 = scene->CreateEntity("skull");
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/skull.obj");
			obj5.emplace<RendererComponent>().SetMesh(vao).SetMaterial(snowMat);
			obj5.get<Transform>().SetLocalPosition(-5.0f, 15.0f, -0.5f);
			obj5.get<Transform>().SetLocalRotation(180.0f, 20.0f, 30.0f);
			obj5.get<Transform>().SetLocalScale(glm::vec3(2.0f));
			//BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(obj3);
		}

		GameObject obj6 = scene->CreateEntity("skullTombstone");
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/skull.obj");
			obj6.emplace<RendererComponent>().SetMesh(vao).SetMaterial(snowMat);
			obj6.get<Transform>().SetLocalPosition(-2.0f, 2.7f, -2.5f);
			obj6.get<Transform>().SetLocalRotation(500.0f, 0.0f, 30.0f);
			obj6.get<Transform>().SetLocalScale(glm::vec3(1.0f));
			//BehaviourBinding::BindDisabled<SimpleMoveBehaviour>(obj3);
		}

		//Animated Skeleton
		GameObject obj7 = scene->CreateEntity("skeleton");
		{
			VertexArrayObject::sptr vao = ObjLoader::LoadFromFile("models/skelleton_final.obj");
			obj7.emplace<RendererComponent>().SetMesh(vao).SetMaterial(snowMat);
			obj7.get<Transform>().SetLocalPosition(0.0f, -10.0f, 0.0f);
			obj7.get<Transform>().SetLocalRotation(90.0f, 0.0f, 0.0f);
			obj7.get<Transform>().SetLocalScale(glm::vec3(3.0f));
			//BehaviourBinding::Bind<SimpleMoveBehaviour>(obj7);

			//Bind returns a smart pointer to the behaviour that was added
			auto pathing = BehaviourBinding::Bind <FollowPathBehaviour>(obj7);
			//Set up a path for the object to follow
			pathing->Points.push_back({ -4.0f, -4.0f, 0.0f });
			pathing->Points.push_back({  4.0f, -4.0f, 0.0f });
			pathing->Points.push_back({  4.0f,  4.0f, 0.0f });
			pathing->Points.push_back({ -4.0f,  4.0f, 0.0f });
			pathing->Speed = 2.0f;
		}


		std::vector<glm::vec2> allAvoidAreasFrom = { glm::vec2(-7.0f, -7.0f) };
		std::vector<glm::vec2> allAvoidAreasTo = { glm::vec2(7.0f, 7.0f) };

		std::vector<glm::vec2> rockAvoidAreasFrom = { glm::vec2(-3.0f, -3.0f), glm::vec2(-19.0f, -19.0f), glm::vec2(5.0f, -19.0f),
														glm::vec2(-19.0f, 5.0f), glm::vec2(-19.0f, -19.0f) };
		std::vector<glm::vec2> rockAvoidAreasTo = { glm::vec2(3.0f, 3.0f), glm::vec2(19.0f, -5.0f), glm::vec2(19.0f, 19.0f),
														glm::vec2(19.0f, 19.0f), glm::vec2(-5.0f, 19.0f) };
		glm::vec2 spawnFromHere = glm::vec2(-18.0f, -18.0f);
		glm::vec2 spawnToHere = glm::vec2(18.0f, 18.0f);

		EnvironmentGenerator::AddObjectToGeneration("models/grass.obj", grassleafMat, 200,
			spawnFromHere, spawnToHere, allAvoidAreasFrom, allAvoidAreasTo);
		EnvironmentGenerator::AddObjectToGeneration("models/mushroom.obj", mooshMat, 50,
			spawnFromHere, spawnToHere, allAvoidAreasFrom, allAvoidAreasTo);
		EnvironmentGenerator::AddObjectToGeneration("models/simpleRock.obj", simpleFloraMat, 10,
			spawnFromHere, spawnToHere, rockAvoidAreasFrom, rockAvoidAreasTo);
		EnvironmentGenerator::AddObjectToGeneration("models/flower.obj", flowerMat, 10,
			spawnFromHere, spawnToHere, rockAvoidAreasFrom, rockAvoidAreasTo);
		EnvironmentGenerator::AddObjectToGeneration("models/bush.obj", bushMat, 3,
			spawnFromHere, spawnToHere, rockAvoidAreasFrom, rockAvoidAreasTo);
		EnvironmentGenerator::GenerateEnvironment();

		// Create an object to be our camera
		GameObject cameraObject = scene->CreateEntity("Camera");
		{
			cameraObject.get<Transform>().SetLocalPosition(0, 3, 3).LookAt(glm::vec3(0, 0, 0));

			// We'll make our camera a component of the camera object
			Camera& camera = cameraObject.emplace<Camera>();// Camera::Create();
			camera.SetPosition(glm::vec3(0, 3, 3));
			camera.SetUp(glm::vec3(0, 0, 1));
			camera.LookAt(glm::vec3(0));
			camera.SetFovDegrees(90.0f); // Set an initial FOV
			camera.SetOrthoHeight(3.0f);
			BehaviourBinding::Bind<CameraControlBehaviour>(cameraObject);
		}

		Framebuffer* testBuffer;
		GameObject framebufferObject = scene->CreateEntity("Basic Buffer");
		{
			int width, height;
			glfwGetWindowSize(BackendHandler::window, &width, &height);

			testBuffer = &framebufferObject.emplace<Framebuffer>();
			testBuffer->AddDepthTarget();
			testBuffer->AddColorTarget(GL_RGBA8);
			testBuffer->Init(width, height);
		}
		#pragma endregion 
		//////////////////////////////////////////////////////////////////////////////////////////

		/////////////////////////////////// SKYBOX ///////////////////////////////////////////////
		{
			// Load our shaders
			Shader::sptr skybox = std::make_shared<Shader>();
			skybox->LoadShaderPartFromFile("shaders/skybox-shader.vert.glsl", GL_VERTEX_SHADER);
			skybox->LoadShaderPartFromFile("shaders/skybox-shader.frag.glsl", GL_FRAGMENT_SHADER);
			skybox->Link();

			ShaderMaterial::sptr skyboxMat = ShaderMaterial::Create();
			skyboxMat->Shader = skybox;  
			skyboxMat->Set("s_Environment", environmentMap);
			skyboxMat->Set("u_EnvironmentRotation", glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1, 0, 0))));
			skyboxMat->RenderLayer = 100;

			MeshBuilder<VertexPosNormTexCol> mesh;
			MeshFactory::AddIcoSphere(mesh, glm::vec3(0.0f), 1.0f);
			MeshFactory::InvertFaces(mesh);
			VertexArrayObject::sptr meshVao = mesh.Bake();
			
			GameObject skyboxObj = scene->CreateEntity("skybox");  
			skyboxObj.get<Transform>().SetLocalPosition(0.0f, 0.0f, 0.0f);
			skyboxObj.get_or_emplace<RendererComponent>().SetMesh(meshVao).SetMaterial(skyboxMat);
		}
		////////////////////////////////////////////////////////////////////////////////////////

		// We'll use a vector to store all our key press events for now (this should probably be a behaviour eventually)
		std::vector<KeyPressWatcher> keyToggles;
		{
			// This is an example of a key press handling helper. Look at InputHelpers.h an .cpp to see
			// how this is implemented. Note that the ampersand here is capturing the variables within
			// the scope. If you wanted to do some method on the class, your best bet would be to give it a method and
			// use std::bind
			keyToggles.emplace_back(GLFW_KEY_T, [&]() { cameraObject.get<Camera>().ToggleOrtho(); });

			controllables.push_back(obj2);

			keyToggles.emplace_back(GLFW_KEY_KP_ADD, [&]() {
				BehaviourBinding::Get<SimpleMoveBehaviour>(controllables[selectedVao])->Enabled = false;
				selectedVao++;
				if (selectedVao >= controllables.size())
					selectedVao = 0;
				BehaviourBinding::Get<SimpleMoveBehaviour>(controllables[selectedVao])->Enabled = true;
				});
			keyToggles.emplace_back(GLFW_KEY_KP_SUBTRACT, [&]() {
				BehaviourBinding::Get<SimpleMoveBehaviour>(controllables[selectedVao])->Enabled = false;
				selectedVao--;
				if (selectedVao < 0)
					selectedVao = controllables.size() - 1;
				BehaviourBinding::Get<SimpleMoveBehaviour>(controllables[selectedVao])->Enabled = true;
				});

			keyToggles.emplace_back(GLFW_KEY_Y, [&]() {
				auto behaviour = BehaviourBinding::Get<SimpleMoveBehaviour>(controllables[selectedVao]);
				behaviour->Relative = !behaviour->Relative;

				});


			//"1" Lighting ON/OFF
			keyToggles.emplace_back(GLFW_KEY_1, [&]()
			{

				if (lighton) {
					shader->SetUniform("u_LightPos", glm::vec3(0, 0, -1000));
					shader->SetUniform("u_LightAttenuationLinear", float(0.019));
					shader->SetUniform("u_LightAttenuationQuadratic", float(0.5));
					lighton = false;
				}
				else {
					shader->SetUniform("u_LightPos", glm::vec3(0, 0, 10));
					shader->SetUniform("u_LightAttenuationLinear", float(0.0));
					shader->SetUniform("u_LightAttenuationQuadratic", float(0.0));
					lighton = true;
				}
			});
			
			//"2" Ambient Lighting ONLY ON/OFF
			keyToggles.emplace_back(GLFW_KEY_2, [&]() {

				if (lightAmbientPow > 0) {
					lightAmbientPow = 0;
					lightSpecularPow = 0;
					shader->SetUniform("u_AmbientLightStrength", lightAmbientPow);
					shader->SetUniform("u_SpecularLightStrength", lightSpecularPow);
			
				}
				else {
					lightAmbientPow = 1;
					lightSpecularPow = 0;
					shader->SetUniform("u_AmbientLightStrength", lightAmbientPow);
					shader->SetUniform("u_SpecularLightStrength", lightSpecularPow);
					
				}
			});

			//"3" Specular Lighting ONLY ON/OFF
			keyToggles.emplace_back(GLFW_KEY_3, [&]() {

				if (lightSpecularPow > 0) {
					lightAmbientPow = 0;
					lightSpecularPow = 0;
					shader->SetUniform("u_AmbientLightStrength", lightAmbientPow);
					shader->SetUniform("u_SpecularLightStrength", lightSpecularPow);
				}
				else {
					lightAmbientPow = 0;
					lightSpecularPow = 1;
					shader->SetUniform("u_AmbientLightStrength", lightAmbientPow);
					shader->SetUniform("u_SpecularLightStrength", lightSpecularPow);
				}
			});

			//"4" Ambient & Specular ON/OFF
			keyToggles.emplace_back(GLFW_KEY_4, [&]() {

				if (lightSpecularPow > 0) {
					lightAmbientPow = 0;
					lightSpecularPow = 0;
					shader->SetUniform("u_AmbientLightStrength", lightAmbientPow);
					shader->SetUniform("u_SpecularLightStrength", lightSpecularPow);
				}
				else {
					lightAmbientPow = 1;
					lightSpecularPow = 1;
					shader->SetUniform("u_AmbientLightStrength", lightAmbientPow);
					shader->SetUniform("u_SpecularLightStrength", lightSpecularPow);
				}
			});

			//"5" Ambient & Specular & Effect ON/OFF
			keyToggles.emplace_back(GLFW_KEY_5, [&]() {

				if (lightSpecularPow > 0) {
					lightAmbientPow = 0;
					lightSpecularPow = 0;
					diffuse = 0;
					shader->SetUniform("u_AmbientLightStrength", lightAmbientPow);
					shader->SetUniform("u_SpecularLightStrength", lightSpecularPow);
					shader->SetUniform("u_toonShading", diffuse);

				}
				else {
					lightAmbientPow = 1;
					lightSpecularPow = 1;
					diffuse = 1;
					shader->SetUniform("u_AmbientLightStrength", lightAmbientPow);
					shader->SetUniform("u_SpecularLightStrength", lightSpecularPow);
					shader->SetUniform("u_toonShading", diffuse);
				}
			});
		}

		// Initialize our timing instance and grab a reference for our use
		Timing& time = Timing::Instance();
		time.LastFrame = glfwGetTime();

		///// Game loop /////
		while (!glfwWindowShouldClose(BackendHandler::window)) {
			glfwPollEvents();

			// Update the timing
			time.CurrentFrame = glfwGetTime();
			time.DeltaTime = static_cast<float>(time.CurrentFrame - time.LastFrame);

			time.DeltaTime = time.DeltaTime > 1.0f ? 1.0f : time.DeltaTime;

			// Update our FPS tracker data
			fpsBuffer[frameIx] = 1.0f / time.DeltaTime;
			frameIx++;
			if (frameIx >= 128)
				frameIx = 0;

			// We'll make sure our UI isn't focused before we start handling input for our game
			if (!ImGui::IsAnyWindowFocused()) {
				// We need to poll our key watchers so they can do their logic with the GLFW state
				// Note that since we want to make sure we don't copy our key handlers, we need a const
				// reference!
				for (const KeyPressWatcher& watcher : keyToggles) {
					watcher.Poll(BackendHandler::window);
				}
			}

			// Iterate over all the behaviour binding components
			scene->Registry().view<BehaviourBinding>().each([&](entt::entity entity, BehaviourBinding& binding) {
				// Iterate over all the behaviour scripts attached to the entity, and update them in sequence (if enabled)
				for (const auto& behaviour : binding.Behaviours) {
					if (behaviour->Enabled) {
						behaviour->Update(entt::handle(scene->Registry(), entity));
					}
				}
			});

			// Clear the screen
			testBuffer->Clear();

			glClearColor(0.08f, 0.17f, 0.31f, 1.0f);
			glEnable(GL_DEPTH_TEST);
			glClearDepth(1.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			// Update all world matrices for this frame
			scene->Registry().view<Transform>().each([](entt::entity entity, Transform& t) {
				t.UpdateWorldMatrix();
			});
			
			// Grab out camera info from the camera object
			Transform& camTransform = cameraObject.get<Transform>();
			glm::mat4 view = glm::inverse(camTransform.LocalTransform());
			glm::mat4 projection = cameraObject.get<Camera>().GetProjection();
			glm::mat4 viewProjection = projection * view;
				

			//Adding rotations to transformation animation
			/*
			if(obj7.get<Transform>().GetLocalPosition().y< -3.6&& obj7.get<Transform>().GetLocalPosition().y)
			{
				obj7.get<Transform>().SetLocalRotation(0.0f, 0.0f, 90.0f);
			}

			std::cout << obj7.get<Transform>().GetLocalPosition().x << std::endl;
			std::cout << obj7.get<Transform>().GetLocalPosition().y << std::endl;
			std::cout << obj7.get<Transform>().GetLocalPosition().z << std::endl;
			std::cout << std::endl;

			*/

			// Sort the renderers by shader and material, we will go for a minimizing context switches approach here,
			// but you could for instance sort front to back to optimize for fill rate if you have intensive fragment shaders
			renderGroup.sort<RendererComponent>([](const RendererComponent& l, const RendererComponent& r) {
				// Sort by render layer first, higher numbers get drawn last
				if (l.Material->RenderLayer < r.Material->RenderLayer) return true;
				if (l.Material->RenderLayer > r.Material->RenderLayer) return false;

				// Sort by shader pointer next (so materials using the same shader run sequentially where possible)
				if (l.Material->Shader < r.Material->Shader) return true;
				if (l.Material->Shader > r.Material->Shader) return false;

				// Sort by material pointer last (so we can minimize switching between materials)
				if (l.Material < r.Material) return true;
				if (l.Material > r.Material) return false;
				
				return false;
			});

			// Start by assuming no shader or material is applied
			Shader::sptr current = nullptr;
			ShaderMaterial::sptr currentMat = nullptr;

			testBuffer->Bind();

			// Iterate over the render group components and draw them
			renderGroup.each( [&](entt::entity e, RendererComponent& renderer, Transform& transform) {
				// If the shader has changed, set up it's uniforms
				if (current != renderer.Material->Shader) {
					current = renderer.Material->Shader;
					current->Bind();
					BackendHandler::SetupShaderForFrame(current, view, projection);
				}
				// If the material has changed, apply it
				if (currentMat != renderer.Material) {
					currentMat = renderer.Material;
					currentMat->Apply();
				}
				// Render the mesh
				BackendHandler::RenderVAO(renderer.Material->Shader, renderer.Mesh, viewProjection, transform);
			});

			testBuffer->Unbind();

			testBuffer->DrawToBackbuffer();

			// Draw our ImGui content
			BackendHandler::RenderImGui();

			scene->Poll();
			glfwSwapBuffers(BackendHandler::window);
			time.LastFrame = time.CurrentFrame;
		}

		// Nullify scene so that we can release references
		Application::Instance().ActiveScene = nullptr;
		//Clean up the environment generator so we can release references
		EnvironmentGenerator::CleanUpPointers();
		BackendHandler::ShutdownImGui();
	}	

	// Clean up the toolkit logger so we don't leak memory
	Logger::Uninitialize();
	return 0;
}