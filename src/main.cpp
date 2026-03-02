#define PLUGIN_VER 2
//#define PLUGIN_DEBUG
#define PLUGIN_NAME "intellightent-ng"
#define ESP_NAME "intellightent.esp"

struct settings
{
	static void reset()
	{
		iDebugMode = 0;
		iLightCount = 4;
		bTryNormalLight = true;
		iMaxConvertCount = 32;
		sScoreFormula = "lightradius * lightintensity / (1 + ((1 - lightneverfades) * lightdistance) / 1000) * (1 + lightchosenlastframe * 0.3)";
		//sAllowConvert = "";
	}

	static inline int         iDebugMode;
	static inline int         iLightCount;
	static inline bool        bTryNormalLight;
	static inline int         iMaxConvertCount;
	static inline std::string sScoreFormula;
	static inline std::string sAllowConvert;

private:
	struct map_helper_base
	{
		virtual ~map_helper_base() {}
		virtual void read() = 0;
	};

	template <typename T, typename X>
	struct map_helper : map_helper_base
	{
		map_helper(std::string_view section, std::string_view key, T* value) : s(section, key, *value)
		{
			p = value;
		}

		T* p;
		X  s;

		void read() override
		{
			*p = s.GetValue();
		}
	};

	struct map_helper_f : map_helper<float, REX::INI::F32<>>
	{
		using map_helper<float, REX::INI::F32<>>::map_helper;
	};
	struct map_helper_i : map_helper<int, REX::INI::I32<>>
	{
		using map_helper<int, REX::INI::I32<>>::map_helper;
	};
	struct map_helper_b : map_helper<bool, REX::INI::Bool<>>
	{
		using map_helper<bool, REX::INI::Bool<>>::map_helper;
	};
	struct map_helper_s : map_helper<std::string, REX::INI::Str<>>
	{
		using map_helper<std::string, REX::INI::Str<>>::map_helper;
	};

public:
	static bool load()
	{
		auto store = REX::INI::SettingStore::GetSingleton();
		store->Init("Data/SKSE/Plugins/" PLUGIN_NAME ".ini", "Data/SKSE/Plugins/" PLUGIN_NAME ".user.ini");

		REX::INI::I32<> versionSetting("Plugin", "Version", 0);

		std::vector<map_helper_base*> vec;

		const char* section = "Game";

		vec.push_back(new map_helper_i{ section, "iDebugMode", &iDebugMode });
		vec.push_back(new map_helper_i{ section, "iLightCount", &iLightCount });
		vec.push_back(new map_helper_b{ section, "bTryNormalLight", &bTryNormalLight });
		vec.push_back(new map_helper_i{ section, "iMaxConvertCount", &iMaxConvertCount });
		vec.push_back(new map_helper_s{ section, "sScoreFormula", &sScoreFormula });
		vec.push_back(new map_helper_s{ section, "sAllowConvert", &sAllowConvert });

		store->Load();

		for (auto x : vec)
			x->read();

		if (versionSetting.GetValue() < PLUGIN_VER) {
			versionSetting.SetValue(PLUGIN_VER);
			store->Save();
		}

		for (auto x : vec)
			delete x;

		return true;
	}
};

struct gamedata
{
	gamedata()
	{
	}

	RE::TESGlobal* GameHour{ nullptr };
	RE::TESGlobal* DebugCurrentSCLight{ nullptr };
	RE::TESGlobal* DebugActiveSCLight{ nullptr };
	RE::TESGlobal* DebugOverwrite{ nullptr };
	RE::TESGlobal* DebugForceConvert{ nullptr };
	RE::TESGlobal* DebugData{ nullptr };

	bool IsPluginLoaded()
	{
		return DebugCurrentSCLight != nullptr;
	}

	bool init()
	{
		int success = 1;

		const char* myModName = ESP_NAME;
		const char* skyrimName = "Skyrim.esm";

		Lookup(success, GameHour, skyrimName, 0x38, true);

		Lookup(success, DebugCurrentSCLight, myModName, 0xd68, true);
		Lookup(success, DebugActiveSCLight, myModName, 0xd65, true);
		Lookup(success, DebugOverwrite, myModName, 0xd6b, true);
		Lookup(success, DebugForceConvert, myModName, 0x18e5, true);
		Lookup(success, DebugData, myModName, 0x1382, true);

		if (success != 0) {
			// Do additional edits here if needed.
		}

		return success != 0;
	}

private:
	template <typename T>
	void Lookup(int& success, T*& ptr, std::string_view str, uint32_t formId, bool optional = false)
	{
		if (success == 0)
			return;
		ptr = Lookup<T>(str, formId);
		if (!ptr && !optional)
			success = 0;
	}

	template <typename T>
	T* Lookup(std::string_view str, uint32_t formId)
	{
		return RE::TESDataHandler::GetSingleton()->LookupForm<T>(formId, str);
	}
};

uint8_t GAME_VER = 0;

FormulaHelper* g_formulaLightScore = nullptr;
FormulaHelper* g_formulaAllowConvert = nullptr;

std::vector<RE::BSShadowLight*> g_frameConvert;
uint64_t                        g_lastFrameChosen[4]{ 0, 0, 0, 0 };

struct plugin
{
	static const char* init()
	{
		if (const auto intfc = SKSE::GetMessagingInterface())
			intfc->RegisterListener(OnSKSEMessage);
		else
			return "failed to get SKSE messaging interface";

		if (REL::Module::IsSE())
			GAME_VER = 0;
		else if (REL::Module::IsAE())
			GAME_VER = 1;
		/*else if (REL::Module::IsVR())
			GAME_VER = 2;*/
		else
			return "game version is not supported";

		if (!settings::sScoreFormula.empty()) {
			g_formulaLightScore = new FormulaHelper();
			if (!g_formulaLightScore->Parse(settings::sScoreFormula))
				return "failed to parse light score formula";
		}

		if (!settings::sAllowConvert.empty()) {
			g_formulaAllowConvert = new FormulaHelper();
			if (!g_formulaAllowConvert->Parse(settings::sAllowConvert))
				return "failed to parse allowConvert formula";
		}

		if (!Hook_Calculate())
			return "failed at Hook_Calculate";

		if (!Hook_ConvertLights())
			return "failed at Hook_ConvertLights";

		return nullptr;
	}

	static const gamedata* getgamedata()
	{
		auto a = get();
		if (a)
			return &a->_gamedata;
		return nullptr;
	}

private:
	struct _addr
	{
		_addr(uint64_t id, int32_t offset, std::string_view pattern) : _id(id), _offset(offset), _pattern(pattern)
		{
		}

		_addr() : _id(0), _offset(0) {}

		uint64_t         _id;
		int32_t          _offset;
		std::string_view _pattern;

		void* get() const
		{
			auto id = REL::ID(_id);
			auto addr = id.address();
			if (addr == 0)
				return nullptr;

			addr += _offset;

			if (_pattern.data() && !_pattern.empty()) {
				if (!MemoryHelper::TestBytes((void*)addr, _pattern))
					return nullptr;
			}

			return (void*)addr;
		}
	};

	static void* get_addr(const _addr* arr)
	{
		const auto& addr = arr[GAME_VER];
		return addr.get();
	}

	static RE::ShadowSceneNode* GetShadowSceneNode()
	{
		// 141E33F40
		static REL::VariantID uid(513211, 390951, 0);
		return *((RE::ShadowSceneNode**)uid.address());
	}

	static RE::SceneGraph* GetWorldSceneGraph()
	{
		// 143258B48
		static REL::VariantID uid(528087, 415032, 0);
		return *((RE::SceneGraph**)uid.address());
	}

	static bool GetUnknownSunBool1()
	{
		// 141E33EB3
		static REL::VariantID uid(513201, 390932, 0);
		return *((bool*)uid.address());
	}

	static int GetUnknownSunInt1()
	{
		// 1431F6648
		static REL::VariantID uid(527703, 414625, 0);
		return *((int*)uid.address());
	}

	static bool GetUnknownSunBool2()
	{
		// 143258B71
		static REL::VariantID uid(528095, 415040, 0);
		return *((bool*)uid.address());
	}

	static bool* GetSelectedFocusShadows()
	{
		// 143258B72
		static REL::VariantID uid(528096, 415041, 0);
		return (bool*)uid.address();
	}

	static uint64_t* GetUnknownSunPointer1()
	{
		// 1432A9210
		static REL::VariantID uid(528315, 415267, 0);
		return (uint64_t*)uid.address();
	}

	static uint32_t* GetLastFrameActiveShadowCasterLightCount1()
	{
		// 143258B60
		static REL::VariantID uid(528090, 415035, 0);
		return (uint32_t*)uid.address();
	}

	static uint32_t* GetLastFrameActiveShadowCasterLightCount2()
	{
		// 143258B64
		static REL::VariantID uid(528091, 415036, 0);
		return (uint32_t*)uid.address();
	}

	static uint32_t* GetLastFrameActiveShadowCasterLightCount3()
	{
		// 143258B68
		static REL::VariantID uid(528091, 415036, 0);
		return (uint32_t*)(uid.address() + 4);
	}

	static void ApplyLensFlare(RE::BSLight* light)
	{
		// 1412FC470
		using func_t = decltype(&ApplyLensFlare);
		static REL::Relocation<func_t> func{ REL::VariantID(100440, 107157, 0) };
		func(light);
	}

	static void unk_Accumulate(RE::BSShadowLight* light)
	{
		// 14131C890
		using func_t = decltype(&unk_Accumulate);
		static REL::Relocation<func_t> func{ REL::VariantID(100819, 107603, 0) };
		func(light);
	}

	static uint32_t* GetActiveShadowCasterLightMask()
	{
		// 143258B6C
		static REL::VariantID uid(528093, 415038, 0);
		return (uint32_t*)uid.address();
	}

	static void unk_BSPortalGraphEntry_func(RE::BSPortalGraphEntry* entry)
	{
		// 140D3D920
		using func_t = decltype(&unk_BSPortalGraphEntry_func);
		static REL::Relocation<func_t> func{ REL::VariantID(74395, 76119, 0) };
		func(entry);
	}

	static bool unk_BSPortalGraphEntry_func2(RE::BSPortalGraphEntry* first, RE::BSPortalGraphEntry* second)
	{
		// 140D3DA20
		using func_t = decltype(&unk_BSPortalGraphEntry_func2);
		static REL::Relocation<func_t> func{ REL::VariantID(74397, 76121, 0) };
		return func(first, second);
	}

	static RE::BSCullingProcess* GetUnknownGlobalCullingProcess()
	{
		// 143258B00
		static REL::VariantID uid(528077, 415022, 0);
		return **((RE::BSCullingProcess***)uid.address());
	}

	static void unk_BSShadowDirectionalLight_set(RE::BSShadowLight* light, RE::NiCamera* camera)
	{
		// 14131C170
		using func_t = decltype(&unk_BSShadowDirectionalLight_set);
		static REL::Relocation<func_t> func{ REL::VariantID(100817, 107601, 0) };
		func(light, camera);
	}

	static void ShadowSceneNode_unk_EnableLight(RE::ShadowSceneNode* shadowSceneNode, RE::BSLight* light)
	{
		// 1412D2070
		using func_t = decltype(&ShadowSceneNode_unk_EnableLight);
		static REL::Relocation<func_t> func{ REL::VariantID(99708, 106342, 0) };
		func(shadowSceneNode, light);
	}

	static void BSLight_ClearGeometryList(RE::BSLight* light)
	{
		// 141334360
		using func_t = decltype(&BSLight_ClearGeometryList);
		static REL::Relocation<func_t> func{ REL::VariantID(101298, 108285, 0) };
		func(light);
	}

	static void ShadowSceneNode_SetShadowCasterLightArrayEntry(RE::ShadowSceneNode* shadowSceneNode, RE::BSLight* light, uint32_t index, uint32_t unk4)
	{
		// 1412D3B40
		using func_t = decltype(&ShadowSceneNode_SetShadowCasterLightArrayEntry);
		static REL::Relocation<func_t> func{ REL::VariantID(99728, 106365, 0) };
		func(shadowSceneNode, light, index, unk4);
	}

	static int GetUnkView(int index)
	{
		// 143052D18
		// 143052D1C

		static REL::VariantID uid1(524978, 411459, 0);
		static REL::VariantID uid2(524979, 411460, 0);

		if (index == 0)
			return *((int*)uid1.address());
		return *((int*)uid2.address());
	}

	static void NiCamera_unk_CalculateFrustumOverlap(RE::NiCamera* camera, float* coord, float* result1, float* result2, float epsilon)
	{
		// 140C65760
		using func_t = decltype(&NiCamera_unk_CalculateFrustumOverlap);
		static REL::Relocation<func_t> func{ REL::VariantID(69265, 70632, 0) };
		func(camera, coord, result1, result2, epsilon);
	}

	static bool BSLightingShaderProperty_IsLightAffectingSurface(RE::BSLightingShaderProperty* p, RE::BSLight* light)
	{
		// 1412A9410
		using func_t = decltype(&BSLightingShaderProperty_IsLightAffectingSurface);
		static REL::Relocation<func_t> func{ REL::VariantID(98902, 105550, 0) };
		return func(p, light);
	}

	static int addFrameConvert(RE::BSShadowLight* light, RE::NiCamera* camera, RE::ShadowSceneNode* shadowSceneNode)
	{
		if (!light)
			return -1;

		for (auto l : g_frameConvert) {
			if (l == light) {
				OnDecidedToConvert(light, camera, shadowSceneNode, false);
				return 0;
			}
		}

		int c = (int)g_frameConvert.size();
		if (c >= settings::iMaxConvertCount)
			return -1;

		OnDecidedToConvert(light, camera, shadowSceneNode, true);
		g_frameConvert.push_back(light);
		OnDecidedToConvert(light, camera, shadowSceneNode, false);
		return 1;
	}

	static void OnDecidedToDisable(RE::BSShadowLight* light)
	{
		if (!light)
			SKSE::stl::report_and_fail("Called OnDecidedToDisable(...) on a null light in " PLUGIN_NAME "!");

		auto cull = light->cullingProcess;
		if (!cull)
			SKSE::stl::report_and_fail("OnDecidedToDisable(...) had null culling process on a light in " PLUGIN_NAME "!");

		auto portal = cull->portalGraphEntry;
		if (!portal)
			SKSE::stl::report_and_fail("OnDecidedToDisable(...) had null portal graph entry on a light in " PLUGIN_NAME "!");

		if (settings::bTryNormalLight) {
			for (auto itr = g_frameConvert.begin(); itr != g_frameConvert.end(); itr++) {
				if (*itr != light)
					continue;

				BSLight_ClearGeometryList(light);
				g_frameConvert.erase(itr);
				break;
			}
		}

		unk_BSPortalGraphEntry_func(portal);
		light->ClearShadowMapData();
	}

	static void OnDecidedToConvert(RE::BSShadowLight* light, RE::NiCamera* camera, RE::ShadowSceneNode* shadowSceneNode, bool prepass)
	{
		if (!light)
			SKSE::stl::report_and_fail("Called OnDecidedToConvert(...) on a null light in " PLUGIN_NAME "!");

		if (prepass) {
			auto cull = light->cullingProcess;
			if (!cull)
				SKSE::stl::report_and_fail("OnDecidedToConvert(...) had null culling process on a light in " PLUGIN_NAME "!");

			auto portal = cull->portalGraphEntry;
			if (!portal)
				SKSE::stl::report_and_fail("OnDecidedToConvert(...) had null portal graph entry on a light in " PLUGIN_NAME "!");

			unk_BSPortalGraphEntry_func(portal);
			light->ClearShadowMapData();
		} else {
			ShadowSceneNode_unk_EnableLight(shadowSceneNode, light);
		}
	}

	static void OnDecidedToEnable(RE::BSShadowLight* light, RE::NiCamera* camera, RE::ShadowSceneNode* shadowSceneNode, int doneLightCount, bool isDelayed = false)
	{
		if (!light)
			SKSE::stl::report_and_fail("Called OnDecidedToEnable(...) on a null light in " PLUGIN_NAME "!");

		if (settings::bTryNormalLight) {
			for (auto itr = g_frameConvert.begin(); itr != g_frameConvert.end(); itr++) {
				if (*itr == light) {
					BSLight_ClearGeometryList(light);
					g_frameConvert.erase(itr);
					break;
				}
			}
		}

		if (isDelayed) {
			if (!light->UpdateCamera(camera))
				SKSE::stl::report_and_fail("OnDecidedToEnable(...) UpdateCamera call failed on light in " PLUGIN_NAME "!");
		}

		if (light->GetRuntimeData().drawFocusShadows || (!*GetSelectedFocusShadows() && light->GetIsFrustumOrDirectionalLight())) {
			unk_BSShadowDirectionalLight_set(light, camera);
			unk_Accumulate(light);
			light->GetRuntimeData().drawFocusShadows = true;
			*GetSelectedFocusShadows() = true;
			*GetUnknownSunPointer1() = (uint64_t)light;
		}

		ShadowSceneNode_unk_EnableLight(shadowSceneNode, light);
		ShadowSceneNode_SetShadowCasterLightArrayEntry(shadowSceneNode, light, *GetLastFrameActiveShadowCasterLightCount2(), 1);
		{
			uint32_t tmp = *GetLastFrameActiveShadowCasterLightCount3();
			light->GetRuntimeData().maskIndex = tmp++;
			*GetLastFrameActiveShadowCasterLightCount3() = tmp;
		}

		auto  nilight = light->light.get();
		float v15 = nilight->world.translate.x;
		float v16 = nilight->world.translate.y;
		float v17 = nilight->world.translate.x - camera->world.translate.x;
		float v18 = nilight->world.translate.z;
		float v19 = nilight->world.translate.y - camera->world.translate.y;
		float v20 = nilight->GetLightRuntimeData().radius.x;
		float v21 = nilight->world.translate.z - camera->world.translate.z;
		float v22 = sqrtf(v19 * v19 + v17 * v17 + v21 * v21);
		float v25, v23, v27, v26, v24, v28;
		if (v22 >= nilight->GetLightRuntimeData().radius.x + camera->GetRuntimeData2().viewFrustum.fNear) {
			float v34[4];
			v34[3] = nilight->GetLightRuntimeData().radius.x;
			v34[0] = v15 - ((v17 * v20) * (1.0f / v22));
			v34[1] = v16 - ((v19 * v20) * (1.0f / v22));
			v34[2] = v18 - ((v21 * v20) * (1.0f / v22));
			float v30[2];
			float v32[2];
			NiCamera_unk_CalculateFrustumOverlap(camera, &v34[0], &v30[0], &v32[0], 0.00001f);
			v23 = v30[1];
			v24 = v30[0];
			v25 = v32[1];
			v26 = v32[0];
		} else {
			v23 = -1.0f;
			*GetActiveShadowCasterLightMask() |= 1 << *GetLastFrameActiveShadowCasterLightCount2();
			v24 = -1.0f;
			v25 = 1.0f;
			v26 = 1.0f;
		}
		v27 = (float)GetUnkView(0);
		v28 = (float)GetUnkView(1);
		float left = (v24 + 1.0f) * 0.5f * v27;
		float right = (v26 + 1.0f) * 0.5f * v27;
		float top = (1.0f - ((v23 + 1.0f) * 0.5f)) * v28;
		float bottom = (1.0f - ((v25 + 1.0f) * 0.5f)) * v28;
		light->GetRuntimeData().projectedBoundingBox = RE::NiRect<std::uint32_t>((uint32_t)left, (uint32_t)right, (uint32_t)top, (uint32_t)bottom);
		uint32_t maskChannel = static_cast<uint32_t>(doneLightCount);
		light->Accumulate(*GetLastFrameActiveShadowCasterLightCount2(), maskChannel, nullptr);
		if (light->lensFlareData)
			ApplyLensFlare(light);
	}

	struct _tmp_l
	{
		RE::BSShadowLight* bslight;
		double             score;
		double             allowConvert;
	};

	static void CalculateActiveShadowCasterLights()
	{
		auto shadowSceneNode = GetShadowSceneNode();
		auto worldSceneGraph = GetWorldSceneGraph();
		auto worldCamera = ((RE::BSSceneGraph*)worldSceneGraph)->GetRuntimeData().camera.get();

		bool     sunBool1 = GetUnknownSunBool1() && GetUnknownSunInt1();
		int      doneLightCount = 0;
		uint64_t isSelectedSun = 0;

		if (!GetUnknownSunBool2()) {
			auto sun = shadowSceneNode->GetRuntimeData().sunShadowDirLight;
			if (sun) {
				uint32_t zero = 0;
				sun->Accumulate(*GetLastFrameActiveShadowCasterLightCount2(), zero, nullptr);

				if (sunBool1) {
					unk_Accumulate(sun);
					sun->GetRuntimeData().drawFocusShadows = true;
					*GetSelectedFocusShadows() = true;
					*GetUnknownSunPointer1() = 0;
				}

				if (sun->lensFlareData)
					ApplyLensFlare(sun);

				doneLightCount = 1;
				isSelectedSun = (uint64_t)sun;
			}
		}

		if (sunBool1 && !*GetSelectedFocusShadows()) {
			for (auto itr = shadowSceneNode->GetRuntimeData().activeShadowLights.begin(); itr != shadowSceneNode->GetRuntimeData().activeShadowLights.end(); itr++) {
				auto l = itr->get();
				if (!l)
					continue;

				if ((uint64_t)l == *GetUnknownSunPointer1() && !*GetSelectedFocusShadows()) {
					l->GetRuntimeData().drawFocusShadows = true;
					*GetSelectedFocusShadows() = true;
				} else
					l->GetRuntimeData().drawFocusShadows = false;
			}
		}

		*GetUnknownSunPointer1() = 0;

		auto* data = &get()->_gamedata;

		//clearFrameConvert();

		int32_t thisFrameIndex = 0;

		if (shadowSceneNode->GetRuntimeData().activeShadowLights.size() > 0) {
			std::vector<_tmp_l> vec;

			SetupSceneFormula(worldCamera, shadowSceneNode);

			int32_t tmpIndex = 0;
			for (auto itr = shadowSceneNode->GetRuntimeData().activeShadowLights.begin(); itr != shadowSceneNode->GetRuntimeData().activeShadowLights.end(); itr++) {
				auto l = itr->get();
				if (!l)
					continue;

				auto& e = vec.emplace_back();
				e.bslight = l;
				e.score = CalculateLightScore(l, worldCamera, tmpIndex++, shadowSceneNode);
				e.allowConvert = 1.0;
				if (g_formulaAllowConvert)
					e.allowConvert = g_formulaAllowConvert->Calculate();
			}
			std::sort(vec.begin(), vec.end(), _SortFunc);

			for (int i = 0; i < 4; i++)
				g_lastFrameChosen[i] = 0;

			if (data && data->DebugCurrentSCLight)
				data->DebugCurrentSCLight->value = (float)(doneLightCount + (int32_t)vec.size());

			int debugConvert = 0;
			if (data && data->DebugForceConvert)
				debugConvert = (int)data->DebugForceConvert->value;

			for (auto itr = vec.begin(); itr != vec.end(); itr++) {
				auto                    l = itr->bslight;
				RE::BSCullingProcess*   cull;
				RE::BSPortalGraphEntry* portal;
				if (doneLightCount < settings::iLightCount && debugConvert <= 0 && l->UpdateCamera(worldCamera) && (cull = l->GetRuntimeData().shadowmapDescriptors.front().cullingProcess) != nullptr && (portal = cull->portalGraphEntry) != nullptr && unk_BSPortalGraphEntry_func2(GetUnknownGlobalCullingProcess()->portalGraphEntry, portal)) {
					OnDecidedToEnable(l, worldCamera, shadowSceneNode, doneLightCount);
					doneLightCount++;

					if (thisFrameIndex < 4)
						g_lastFrameChosen[thisFrameIndex++] = (uint64_t)l;
				} else if (settings::bTryNormalLight && debugConvert >= 0 && doneLightCount >= settings::iLightCount && itr->allowConvert >= 0.5 && l->UpdateCamera(worldCamera) && (cull = l->GetRuntimeData().shadowmapDescriptors.front().cullingProcess) != nullptr && (portal = cull->portalGraphEntry) != nullptr && unk_BSPortalGraphEntry_func2(GetUnknownGlobalCullingProcess()->portalGraphEntry, portal)) {
					int converted = addFrameConvert(l, worldCamera, shadowSceneNode);
					if (converted >= 0) {
						// this is now done in addFrameConvert
						//OnDecidedToConvert(l, worldCamera, shadowSceneNode);
					} else
						OnDecidedToDisable(l);
				} else {
					OnDecidedToDisable(l);
				}
			}

			if (data && data->DebugActiveSCLight)
				data->DebugActiveSCLight->value = (float)(doneLightCount);
		}

		if (data && data->DebugData)
			data->DebugData->value = (float)g_frameConvert.size();

		shadowSceneNode->GetRuntimeData().firstPersonShadowMask = *GetActiveShadowCasterLightMask();
		*GetLastFrameActiveShadowCasterLightCount1() = (uint32_t)doneLightCount;
	}

	static bool _SortFunc(const _tmp_l& first, const _tmp_l& second)
	{
		return first.score > second.score;
	}

	static void SetupSceneFormula(RE::NiCamera* camera, RE::ShadowSceneNode* shadowSceneNode)
	{
		FormulaHelper::SetParam(FormulaParams::kFormulaParam_CameraX, camera->world.translate.x);
		FormulaHelper::SetParam(FormulaParams::kFormulaParam_CameraY, camera->world.translate.y);
		FormulaHelper::SetParam(FormulaParams::kFormulaParam_CameraZ, camera->world.translate.z);

		FormulaHelper::SetParam(FormulaParams::kFormulaParam_IsInterior, 0);

		auto plr = RE::PlayerCharacter::GetSingleton();
		if (plr) {
			auto cell = plr->parentCell;
			if (cell && cell->IsInteriorCell())
				FormulaHelper::SetParam(FormulaParams::kFormulaParam_IsInterior, 1);
		}

		auto a = get();
		if (a) {
			if (a->_gamedata.GameHour)
				FormulaHelper::SetParam(FormulaParams::kFormulaParam_TimeOfDay, a->_gamedata.GameHour->value);
		}
	}

	static void SetupLightFormula(RE::BSShadowLight* light, RE::NiCamera* camera, RE::ShadowSceneNode* shadowSceneNode, int32_t index)
	{
		FormulaHelper::SetParam(FormulaParams::kFormulaParam_LightIndex, index);

		double chosenLastFrame = 0.0;
		for (int i = 0; i < 4; i++) {
			if (g_lastFrameChosen[i] == (uint64_t)light) {
				chosenLastFrame = 1.0;
				break;
			}
		}
		FormulaHelper::SetParam(FormulaParams::kFormulaParam_LightChosenLastFrame, chosenLastFrame);

		FormulaHelper::SetParam(FormulaParams::kFormulaParam_LightNeverFades, light->lodFade ? 0.0 : 1.0);  // lodFade false means the light never fades (never fades = 1.0)
		FormulaHelper::SetParam(FormulaParams::kFormulaParam_LightPortalStrict, light->portalStrict ? 1.0 : 0.0);

		float x, y, z;

		auto nilight = light->light.get();
		if (nilight) {
			FormulaHelper::SetParam(FormulaParams::kFormulaParam_LightIntensity, nilight->GetLightRuntimeData().fade);
			FormulaHelper::SetParam(FormulaParams::kFormulaParam_LightRadius, nilight->GetLightRuntimeData().radius.x);
			FormulaHelper::SetParam(FormulaParams::kFormulaParam_LightR, nilight->GetLightRuntimeData().diffuse.red);
			FormulaHelper::SetParam(FormulaParams::kFormulaParam_LightG, nilight->GetLightRuntimeData().diffuse.green);
			FormulaHelper::SetParam(FormulaParams::kFormulaParam_LightB, nilight->GetLightRuntimeData().diffuse.blue);
			FormulaHelper::SetParam(FormulaParams::kFormulaParam_LightAmbientR, nilight->GetLightRuntimeData().ambient.red);
			FormulaHelper::SetParam(FormulaParams::kFormulaParam_LightAmbientG, nilight->GetLightRuntimeData().ambient.green);
			FormulaHelper::SetParam(FormulaParams::kFormulaParam_LightAmbientB, nilight->GetLightRuntimeData().ambient.blue);
			x = nilight->world.translate.x;
			y = nilight->world.translate.y;
			z = nilight->world.translate.z;
		} else {
			FormulaHelper::SetParam(FormulaParams::kFormulaParam_LightIntensity, 0.0f);
			FormulaHelper::SetParam(FormulaParams::kFormulaParam_LightRadius, 0.0f);
			FormulaHelper::SetParam(FormulaParams::kFormulaParam_LightR, 1.0f);
			FormulaHelper::SetParam(FormulaParams::kFormulaParam_LightG, 1.0f);
			FormulaHelper::SetParam(FormulaParams::kFormulaParam_LightB, 1.0f);
			FormulaHelper::SetParam(FormulaParams::kFormulaParam_LightAmbientR, 1.0f);
			FormulaHelper::SetParam(FormulaParams::kFormulaParam_LightAmbientG, 1.0f);
			FormulaHelper::SetParam(FormulaParams::kFormulaParam_LightAmbientB, 1.0f);
			x = light->worldTranslate.x;  // these appear to be 0 always?
			y = light->worldTranslate.y;
			z = light->worldTranslate.z;
		}

		FormulaHelper::SetParam(FormulaParams::kFormulaParam_LightX, x);
		FormulaHelper::SetParam(FormulaParams::kFormulaParam_LightY, y);
		FormulaHelper::SetParam(FormulaParams::kFormulaParam_LightZ, z);

		float dx = x - camera->world.translate.x;
		float dy = y - camera->world.translate.y;
		float dz = z - camera->world.translate.z;
		float dist = sqrtf(dx * dx + dy * dy + dz * dz);
		FormulaHelper::SetParam(FormulaParams::kFormulaParam_LightDistance, dist);
	}

	static double CalculateLightScore(RE::BSShadowLight* light, RE::NiCamera* camera, int32_t index, RE::ShadowSceneNode* shadowSceneNode)
	{
		int debug = settings::iDebugMode;
		{
			auto gb = get()->_gamedata.DebugOverwrite;
			if (gb && gb->value != 0.0f)
				debug = (int)gb->value;
		}
		if (debug != 0) {
			double debugScore = 0.0;
			switch (std::abs(debug)) {
			case 1:
				debugScore = index;
				break;
			case 2:
				{
					auto nilight = light->light.get();
					if (nilight)
						debugScore = nilight->world.translate.GetDistance(camera->world.translate);
				}
				break;
			case 3:
				{
					auto ni = light->light.get();
					if (ni)
						debugScore = ni->GetLightRuntimeData().radius.x;
				}
				break;
			case 4:
				{
					auto ni = light->light.get();
					if (ni)
						debugScore = ni->GetLightRuntimeData().fade;
				}
				break;
			}

			if (debug > 0)
				debugScore = -debugScore;

			return debugScore;
		}

		SetupLightFormula(light, camera, shadowSceneNode, index);

		if (g_formulaLightScore)
			return g_formulaLightScore->Calculate();

		return 0.0;
	}

	static bool Hook_Calculate()
	{
		static _addr addr[] = {
			_addr(100419, 0, "48 89 6C 24 18"),
			_addr(107137, 0, "48 89 6C 24 18"),
		};

		void* a = get_addr(addr);
		void* cave = HookHelper::FindCodeCave(a);
		if (!cave)
			return false;
		HookHelper::WriteAbsoluteJump(cave, reinterpret_cast<void*>(&CalculateActiveShadowCasterLights));
		HookHelper::WriteRelJump(a, cave);

		return true;
	}

	static bool BSShadowLight_IsShadowLight(RE::BSShadowLight* light)
	{
		bool result = true;

		for (auto itr = g_frameConvert.begin(); itr != g_frameConvert.end(); itr++) {
			if (*itr == light) {
				result = false;
				break;
			}
		}

		return result;
	}

	/*static void _Hook_BSShadowLight_IsShadowLight(CONTEXT& ctx)
	{
		ctx.Rax = BSShadowLight_IsShadowLight((RE::BSShadowLight*)ctx.Rcx) ? 1 : 0;
	}*/

	static bool Hook_ConvertLights()
	{
		if (!settings::bTryNormalLight)
			return true;

		{
			// 14131D2A0
			/*static _addr addr[] = {
				_addr(100832, 0, "B0 01 C3 CC CC CC"),
				_addr(107620, 0, "B0 01 C3 CC CC CC"),
			};

			void* a = get_addr(addr);

			if (!MemoryHelper::WriteByte(MemoryHelper::AddPointer(a, 5), 0xC3))
				return false;

			if (!HookHelper::WriteHook(a, 5, 0, _Hook_BSShadowLight_IsShadowLight))
				return false;*/

			// This is faster way to hook since it gets called a lot per frame
			REL::Relocation<std::uintptr_t> vtbl1{ RE::BSShadowLight::VTABLE[0] };
			vtbl1.write_vfunc(3, BSShadowLight_IsShadowLight);
			REL::Relocation<std::uintptr_t> vtbl2{ RE::BSShadowDirectionalLight::VTABLE[0] };
			vtbl2.write_vfunc(3, BSShadowLight_IsShadowLight);
			REL::Relocation<std::uintptr_t> vtbl3{ RE::BSShadowFrustumLight::VTABLE[0] };
			vtbl3.write_vfunc(3, BSShadowLight_IsShadowLight);
			REL::Relocation<std::uintptr_t> vtbl4{ RE::BSShadowParabolicLight::VTABLE[0] };
			vtbl4.write_vfunc(3, BSShadowLight_IsShadowLight);
		}

		{
			// 1412D0DE0
			static _addr addr[] = {
				_addr(99697, 0x9, "53 55 56 57 41 56"),
				_addr(106331, 0x9, "53 48 83 EC 30"),
			};

			void* a = get_addr(addr);

			int len = 6;
			if (GAME_VER == 1)
				len = 5;

			if (!HookHelper::WriteHook(a, len, len, _Hook_ConvertLights_Remove))
				return false;
		}

		return true;
	}

	static void _Hook_ConvertLights_Remove(CONTEXT& ctx)
	{
		RE::ShadowSceneNode* shadowSceneNode = (RE::ShadowSceneNode*)ctx.Rcx;
		if (shadowSceneNode != GetShadowSceneNode())
			return;

		RE::NiLight* light = (RE::NiLight*)ctx.Rdx;
		for (auto itr = g_frameConvert.begin(); itr != g_frameConvert.end(); itr++) {
			auto l = (*itr)->light.get();
			if (l && l == light) {
				BSLight_ClearGeometryList(*itr);
				g_frameConvert.erase(itr);
				break;
			}
		}
	}

	static void OnSKSEMessage(SKSE::MessagingInterface::Message* msg)
	{
		switch (msg->type) {
		case SKSE::MessagingInterface::kNewGame:
		case SKSE::MessagingInterface::kPreLoadGame:
			{
			}
			break;

		case SKSE::MessagingInterface::kDataLoaded:
			{
				auto a = get();
				a->_gamedata = gamedata();
				if (!a->_gamedata.init())
					RE::stl::report_and_fail("Failed to init gamedata! Make sure the " ESP_NAME " plugin is loaded.");
			}
			break;
		}
	}

	plugin()
	{
	}

	gamedata _gamedata;

	static plugin* get()
	{
		static plugin i;
		return &i;
	}
};

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
#ifdef PLUGIN_DEBUG
	//while (!IsDebuggerPresent()) Sleep(1);
#endif

	SKSE::Init(a_skse);

	settings::reset();

	if (!settings::load())  // this won't return false if config is missing, only when something goes really wrong
	{
		SKSE::stl::report_and_fail("Failed to init config file for " PLUGIN_NAME "!");
		return false;
	}

	const char* error = plugin::init();
	if (error) {
		char msg[512];
		_snprintf_s(msg, 512, "Failed to init %s plugin! error: %s", PLUGIN_NAME, error);
		SKSE::stl::report_and_fail(msg);
		return false;
	}

	logs::info("Plugin inited");

	return true;
}
