#define PLUGIN_VER 3
//#define PLUGIN_DEBUG
#define PLUGIN_NAME "intellightent-ng"
#define ESP_NAME "intellightent.esp"
//#define PLUGIN_DEBUG_FRAME
//#define L_MEASURE_PERFORMANCE
//#define USE_ACCUM_SET

int64_t GetPerfCounter()
{
	LARGE_INTEGER counter;
	QueryPerformanceCounter(&counter);

	int64_t t = (int64_t)counter.QuadPart;

	static int64_t freq = 0;
	if (freq == 0) {
		LARGE_INTEGER f;
		QueryPerformanceFrequency(&f);

		freq = f.QuadPart / 1000000;
	}

	return t / freq;
}

#ifdef L_MEASURE_PERFORMANCE
#	define TIMEENTRY_MAX_COUNT 32
#	define TIMEENTRY_TYPES 2

struct timeentry
{
	timeentry()
	{
		prog = 0;
		ptype = -1;
	}

	std::deque<int64_t> last[TIMEENTRY_TYPES];
	int64_t             prog;
	int32_t             ptype;

	int64_t calculateAvgTimeTaken(int32_t type = -1)
	{
		if (type < 0) {
			int64_t sum = 0;
			for (int i = 0; i < TIMEENTRY_TYPES; i++)
				sum += calculateAvgTimeTaken(i);
			return sum;
		} else {
			int64_t sum = 0;
			int32_t count = 0;
			for (auto x : last[type]) {
				sum += x;
				count++;
			}

			if (count > 1)
				sum /= count;

			return sum;
		}
	}

	int64_t calculateMinTimeTaken(int32_t type = -1)
	{
		if (type < 0) {
			int64_t sum = 0;
			for (int i = 0; i < TIMEENTRY_TYPES; i++)
				sum += calculateMinTimeTaken(i);
			return sum;
		} else {
			int64_t low = -1;
			for (auto x : last[type]) {
				if (low < 0 || x < low)
					low = x;
			}

			if (low >= 0)
				return low;

			return 0;
		}
	}

	int64_t calculateMaxTimeTaken(int32_t type = -1)
	{
		if (type < 0) {
			int64_t sum = 0;
			for (int i = 0; i < TIMEENTRY_TYPES; i++)
				sum += calculateMaxTimeTaken(i);
			return sum;
		} else {
			int64_t low = -1;
			for (auto x : last[type]) {
				if (low < 0 || x > low)
					low = x;
			}

			if (low >= 0)
				return low;

			return 0;
		}
	}

	void begin(int32_t type)
	{
		if (ptype >= 0 || type < 0)
			throw std::exception();

		ptype = type;
		prog = GetPerfCounter();
	}

	void end(int32_t type)
	{
		if (ptype != type || type < 0)
			throw std::exception();

		ptype = -1;
		int64_t now = GetPerfCounter();
		int64_t diff = now - prog;
		prog = 0;

		auto& cont = last[type];
		if ((int32_t)cont.size() >= TIMEENTRY_MAX_COUNT)
			cont.pop_front();

		cont.push_back(diff);
	}
};

struct timemeasure
{
	std::mutex                              lock;
	std::unordered_map<uint64_t, timeentry> lights;

	void beginLight(RE::BSShadowLight* l, int32_t type)
	{
		uint64_t key = (uint64_t)l;

		lock.lock();
		{
			auto& e = lights[key];
			e.begin(type);
		}
		lock.unlock();
	}

	void endLight(RE::BSShadowLight* l, int32_t type)
	{
		uint64_t key = (uint64_t)l;

		lock.lock();
		{
			auto& e = lights[key];
			e.end(type);
		}
		lock.unlock();
	}

	void printAll()
	{
		lock.lock();
		{
			for (auto itr = lights.begin(); itr != lights.end(); itr++) {
#	if TIMEENTRY_TYPES == 2
				logs::info("Light {:X} Avg: {} + {} = {} / Min: {} + {} = {} / Max: {} + {} = {}", itr->first, itr->second.calculateAvgTimeTaken(0), itr->second.calculateAvgTimeTaken(1), itr->second.calculateAvgTimeTaken(), itr->second.calculateMinTimeTaken(0), itr->second.calculateMinTimeTaken(1), itr->second.calculateMinTimeTaken(), itr->second.calculateMaxTimeTaken(0), itr->second.calculateMaxTimeTaken(1), itr->second.calculateMaxTimeTaken());
#	else
				TODO();
#	endif
			}
		}
		lock.unlock();
	}
};

timemeasure g_perf;
#endif

#define LIGHTBUDGET_MAX_TRACK 8

struct LightBudgetEntry
{
	LightBudgetEntry()
	{
		memset(Tracked, 0, sizeof(uint16_t) * LIGHTBUDGET_MAX_TRACK);
	}

	uint64_t Key{ 0 };
	uint16_t Tracked[LIGHTBUDGET_MAX_TRACK];
	int32_t  TrackedCount{ 0 };
	int32_t  LastTrackedHelper{ -1 };
	uint16_t Progress{ 0 };
	int32_t  Current{ 0 };

private:
	int64_t _internalTime{ 0 };

public:
	void BeginStep(int32_t step)
	{
		_internalTime = GetPerfCounter();
	}

	void EndStep(int32_t step, int32_t helperCounter)
	{
		int64_t now = GetPerfCounter();
		int64_t diff = now - _internalTime;

		if (step == 0)
			Progress = (uint16_t)(diff > 0xFFFF ? 0xFFFF : diff);
		else if (step == 1) {
			diff += Progress;

			int32_t ix = (TrackedCount % LIGHTBUDGET_MAX_TRACK);
			Current -= Tracked[ix];
			Tracked[ix] = (uint16_t)(diff > 0xFFFF ? 0xFFFF : diff);
			Current += Tracked[ix];

			TrackedCount++;
			LastTrackedHelper = helperCounter;
		}
	}

	bool IsExpired(int32_t helperCounter)
	{
		if (LastTrackedHelper < 0 || (helperCounter - LastTrackedHelper) >= 600)
			return true;

		return false;
	}
};

struct LightBudgetHelper
{
	LightBudgetHelper()
	{
		_calcCounter = 0;
	}

	void Begin(int32_t step)
	{
		if (step == 0) {
			_calcCounter++;
			if ((_calcCounter % 300) == 0)
				CleanupExpired();
		}
	}

	void End(int32_t step)
	{
	}

	void BeginLight(RE::BSShadowLight* l, int32_t step)
	{
		uint64_t key = (uint64_t)l;

		LightBudgetEntry* e = nullptr;

		auto itr = _map.find(key);
		if (itr == _map.end()) {
			e = new LightBudgetEntry();
			e->Key = key;
			_map[key] = e;
		} else
			e = itr->second;

		e->BeginStep(step);
	}

	void EndLight(RE::BSShadowLight* l, int32_t step)
	{
		uint64_t key = (uint64_t)l;

		LightBudgetEntry* e = nullptr;

		auto itr = _map.find(key);
		if (itr == _map.end())  // this should not happen
			return;

		e = itr->second;
		e->EndStep(step, _calcCounter);
	}

	int32_t GetBudget(RE::BSShadowLight* l)
	{
		uint64_t key = (uint64_t)l;

		LightBudgetEntry* e = nullptr;

		auto itr2 = _map.find(key);
		if (itr2 == _map.end() || itr2->second->TrackedCount == 0) {
			// Light hasn't been measured yet, get average budget cost of all lights
			int64_t sum = 0;
			int32_t count = 0;

			for (auto itr = _map.begin(); itr != _map.end(); itr++) {
				int32_t tmp_sum = itr->second->Current;
				int32_t tmp_count = std::min(LIGHTBUDGET_MAX_TRACK, itr->second->TrackedCount);
				if (tmp_count == 0)
					continue;

				if (tmp_count > 1)
					tmp_sum /= tmp_count;

				sum += tmp_sum;
				count++;
			}

			if (count > 1)
				sum /= count;

			return (int32_t)sum;
		} else {
			int32_t tmp_sum = itr2->second->Current;
			int32_t tmp_count = std::min(LIGHTBUDGET_MAX_TRACK, itr2->second->TrackedCount);

			if (tmp_count > 1)
				tmp_sum /= tmp_count;

			return tmp_sum;
		}
	}

#ifdef PLUGIN_DEBUG_FRAME
	void PrintDebug()
	{
		for (auto itr = _map.begin(); itr != _map.end(); itr++) {
			if (itr->second->TrackedCount == 0)
				continue;

			int32_t              min = -1;
			int32_t              max = -1;
			int32_t              avg = GetBudget((RE::BSShadowLight*)itr->first);
			std::vector<int32_t> tmpvec;

			for (int i = 0; i < LIGHTBUDGET_MAX_TRACK && i < itr->second->TrackedCount; i++) {
				int32_t v = itr->second->Tracked[i];
				if (min < 0 || v < min)
					min = v;

				if (max < 0 || v > max)
					max = v;

				tmpvec.push_back(v);
			}

			std::sort(tmpvec.begin(), tmpvec.end());
			int32_t med = tmpvec[tmpvec.size() / 2];

			logs::info("Light {:X} min: {}; max: {}; avg: {}; med: {}", itr->first, min, max, avg, med);
		}
	}
#endif

private:
	int32_t                                         _calcCounter;
	std::unordered_map<uint64_t, LightBudgetEntry*> _map;

	void CleanupExpired()
	{
		int32_t c = _calcCounter;
		for (auto itr = _map.begin(); itr != _map.end();) {
			if (itr->second->IsExpired(c)) {
				delete itr->second;
				itr = _map.erase(itr);
			} else
				itr++;
		}
	}
};

LightBudgetHelper g_budget;

struct settings
{
	static void reset()
	{
		iDebugMode = 0;
		iLightCount = 4;
		bTryNormalLight = true;
		bTryShadowLight = false;
		iMaxConvertCount = 32;
		iMaxConvertCountShadow = 4;
		iMaxRedrawLightPerFrame = 4;
		sScoreFormula = "lightradius * lightintensity / (1 + ((1 - lightneverfades) * lightdistance) / 1000) * (1 + lightchosenlastframe * 0.3)";
		//sAllowConvert = "";
		//sAllowConvertShadow = "";
		sRedrawLightInterval = "min(10, (max(0, lightdistance - lightradius * 0.5) / 500) / max(0.5, lightintensity)) * (lightconverted * 5 + 1)";
		sRedrawBudget = "1 + isinterior";
		iCSDebugLightCount = 0;
		iCSDebugLightConvertCount = 0;
		bForcePortalStrict = true;
		bForceAllowDrawNewLight = true;
		bCSDisableUselessPass = true;
	}

	static inline int         iDebugMode;
	static inline int         iLightCount;
	static inline bool        bTryNormalLight;
	static inline bool        bTryShadowLight;
	static inline int         iMaxConvertCount;
	static inline int         iMaxConvertCountShadow;
	static inline int         iMaxRedrawLightPerFrame;
	static inline std::string sScoreFormula;
	static inline std::string sAllowConvert;
	static inline std::string sAllowConvertShadow;
	static inline std::string sRedrawLightInterval;
	static inline std::string sRedrawBudget;
	static inline int         iCSDebugLightCount;
	static inline int         iCSDebugLightConvertCount;
	static inline bool        bForcePortalStrict;
	static inline bool        bForceAllowDrawNewLight;
	static inline bool        bCSDisableUselessPass;

private:
	struct map_helper_base
	{
		virtual ~map_helper_base() {}
		virtual void read() = 0;
	};

	template <typename T, typename X>
	struct map_helper : map_helper_base
	{
		map_helper(std::string_view section, std::string_view key, T* value) :
			s(section, key, *value)
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
		vec.push_back(new map_helper_b{ section, "bTryShadowLight", &bTryShadowLight });
		vec.push_back(new map_helper_i{ section, "iMaxConvertCountShadow", &iMaxConvertCountShadow });
		vec.push_back(new map_helper_s{ section, "sAllowConvertShadow", &sAllowConvertShadow });
		vec.push_back(new map_helper_i{ section, "iCSDebugLightCount", &iCSDebugLightCount });
		vec.push_back(new map_helper_i{ section, "iCSDebugLightConvertCount", &iCSDebugLightConvertCount });
		vec.push_back(new map_helper_b{ section, "bForcePortalStrict", &bForcePortalStrict });
		vec.push_back(new map_helper_s{ section, "sRedrawLightInterval", &sRedrawLightInterval });
		vec.push_back(new map_helper_i{ section, "iMaxRedrawLightPerFrame", &iMaxRedrawLightPerFrame });
		vec.push_back(new map_helper_b{ section, "bForceAllowDrawNewLight", &bForceAllowDrawNewLight });
		vec.push_back(new map_helper_b{ section, "bCSDisableUselessPass", &bCSDisableUselessPass });
		vec.push_back(new map_helper_s{ section, "sRedrawBudget", &sRedrawBudget });

		store->Load();

		for (auto x : vec)
			x->read();

		if (versionSetting.GetValue() < PLUGIN_VER) {
			versionSetting.SetValue(PLUGIN_VER);
			store->Save();
		}

		if (settings::bTryShadowLight && !settings::bTryNormalLight)
			settings::bTryShadowLight = false;

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
	RE::TESGlobal* DebugData2{ nullptr };

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
		Lookup(success, DebugData2, myModName, 0x1e48, true);

		if (success != 0) {
			// Do additional edits here if needed.
		}

		/*{
			static bool _debugOnce = false;
			auto shadowMapsSRV = RE::BSGraphics::Renderer::GetSingleton()->GetDepthStencilData().depthStencils[RE::RENDER_TARGET_DEPTHSTENCIL::kSHADOWMAPS].depthSRV;
			if (shadowMapsSRV && !_debugOnce)
			{
				_debugOnce = true;
				D3D11_SHADER_RESOURCE_VIEW_DESC shadowMapsSRVDesc{};
				shadowMapsSRV->GetDesc(&shadowMapsSRVDesc);
				if (shadowMapsSRVDesc.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2DARRAY) {
					uint32_t derived = shadowMapsSRVDesc.Texture2DArray.ArraySize;
					logs::info("derived = {}", derived);
				}
				else
					logs::info("dimension = {}", (int)shadowMapsSRVDesc.ViewDimension);
			}
		}*/

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
FormulaHelper* g_formulaAllowConvertShadow = nullptr;
FormulaHelper* g_formulaRenderInterval = nullptr;
FormulaHelper* g_formulaRedrawBudget = nullptr;

struct c_converted
{
	RE::BSShadowLight* light;
	bool               isNS;
};

std::vector<c_converted> g_normalConvert;
std::set<RE::NiLight*>   g_shadowConvert;
uint64_t*                g_lastFrameChosenBuf = nullptr;
int32_t                  g_lastFrameChosenCount = 0;
#ifdef PLUGIN_DEBUG_FRAME
int g_debugFrame = 0;
#endif

struct c_light_entry
{
	c_light_entry()
	{
	}

	RE::BSShadowLight* Light{ nullptr };
	double             RedrawScore{ 0.0 };
	int32_t            LastDrawnFrame{ -1 };
	bool               RedrawFrame{ false };
	int32_t            Index{ -1 };
	int32_t            DrawIndex{ -1 };
#ifdef USE_ACCUM_SET
	std::set<uint64_t>* Accum{ nullptr };
#endif

	void ClearLight()
	{
		Light = nullptr;
		LastDrawnFrame = -1;
#ifdef USE_ACCUM_SET
		Accum->clear();
#endif
	}
};

struct c_light_container
{
	c_light_container()
	{
		Lights = nullptr;
	}

	~c_light_container()
	{
		if (Lights)
			free(Lights);
	}

	c_light_entry* Lights;
	bool           Sun{ false };
	int            Size{ 0 };

	int32_t FindFreeIndex(bool shadow)
	{
		if (shadow) {
			for (int i = 0; i < settings::iCSDebugLightCount; i++) {
				if (!Lights[i].Light)
					return i;
			}
		} else {
			int max = settings::iCSDebugLightConvertCount + settings::iCSDebugLightCount;
			for (int i = settings::iCSDebugLightCount; i < max; i++) {
				if (!Lights[i].Light)
					return i;
			}
		}

		return -1;
	}
};

c_light_container g_lightsAll;
void**            g_normalDepthBuffer = nullptr;
void**            g_readOnlyDepthBuffer = nullptr;

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
		else if (REL::Module::IsVR())
			GAME_VER = 0;
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

		if (!settings::sAllowConvertShadow.empty()) {
			g_formulaAllowConvertShadow = new FormulaHelper();
			if (!g_formulaAllowConvertShadow->Parse(settings::sAllowConvertShadow))
				return "failed to parse allowConvertShadow formula";
		}

		if (!settings::sRedrawLightInterval.empty()) {
			g_formulaRenderInterval = new FormulaHelper();
			if (!g_formulaRenderInterval->Parse(settings::sRedrawLightInterval))
				return "failed to parse redrawLightInterval formula";
		}

		if (!settings::sRedrawBudget.empty()) {
			g_formulaRedrawBudget = new FormulaHelper();
			if (!g_formulaRedrawBudget->Parse(settings::sRedrawBudget))
				return "failed to parse redrawBudget formula";
		}

		// None of these checks are needed anymore
		/*if (settings::iCSDebugLightCount > 0)
		{
			// This is maybe no longer true and this check can be removed?
			if ((settings::iCSDebugLightCount % 4) != 0) {
				settings::iCSDebugLightCount = 0;
				logs::info("Warning! iCSDebugLightCount must be a multiple of 4. Disabling this option.");
			}

			if (settings::iCSDebugLightCount > 64)
			{
				settings::iCSDebugLightCount = 64;
				logs::info("Warning! iCSDebugLightCount can't exceed 64. Setting to 64.");
			}

			if (settings::iCSDebugLightCount + settings::iCSDebugLightConvertCount > 64)
				logs::info("Warning! iCSDebugLightCount + iCSDebugLightConvertCount exceeds 64. This may cause issues where some lights will not affect surfaces properly if more than 64 shadow lights + shadow lights converted to normal lights are on screen at once.");

			if (settings::iMaxRedrawLightPerFrame > 32)
			{
				logs::info("Warning! iMaxRedrawLightPerFrame exceeds 32. This is not allowed, setting to 32.");
				settings::iMaxRedrawLightPerFrame = 32;
			}
		}*/

		g_lightsAll.Size = 4;
		if (settings::iCSDebugLightCount > 0)
			g_lightsAll.Size = settings::iCSDebugLightCount + std::max(0, settings::iCSDebugLightConvertCount);

		g_lastFrameChosenBuf = (uint64_t*)malloc(sizeof(uint64_t) * g_lightsAll.Size);
		memset(g_lastFrameChosenBuf, 0, sizeof(uint64_t) * g_lightsAll.Size);

		g_lightsAll.Lights = (c_light_entry*)malloc(sizeof(c_light_entry) * g_lightsAll.Size);
		for (int i = 0; i < g_lightsAll.Size; i++) {
			g_lightsAll.Lights[i] = c_light_entry();
			g_lightsAll.Lights[i].Index = i;
#ifdef USE_ACCUM_SET
			g_lightsAll.Lights[i].Accum = new std::set<uint64_t>();
#endif
		}

		if (settings::iCSDebugLightCount > 0) {
			if (!Hook_ExtendLights())
				return "failed at Hook_ExtendLights";

			if (!Hook_ConvertLights())
				return "failed at Hook_ConvertLights";
		} else {
			if (!Hook_Calculate())
				return "failed at Hook_Calculate";

			if (!Hook_ConvertLights())
				return "failed at Hook_ConvertLights";
		}

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
		_addr(uint64_t id, int32_t offset, std::string_view pattern) :
			_id(id), _offset(offset), _pattern(pattern)
		{
		}

		_addr() :
			_id(0), _offset(0) {}

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

#ifdef PLUGIN_DEBUG_FRAME
	static bool IsDebugFrame()
	{
		int now = GetCurrentGameFrameCounter();
		if (now > 0 && now == g_debugFrame)
			return true;

		if ((GetAsyncKeyState(0x25) & 0x8000) != 0)  // left arrow
		{
			g_debugFrame = now;
			return true;
		}

		return false;
	}
#endif

	static void* get_addr(const _addr* arr)
	{
		const auto& addr = arr[GAME_VER];
		return addr.get();
	}

	static int32_t GetCurrentGameFrameCounter()
	{
		// 1430538DC
		static REL::RelocationID uid(525008, 411489);
		return *((int32_t*)uid.address());
	}

	static RE::ShadowSceneNode* GetShadowSceneNode()
	{
		// 141E33F40
		static REL::RelocationID uid(513211, 390951);
		return *((RE::ShadowSceneNode**)uid.address());
	}

	static RE::SceneGraph* GetWorldSceneGraph()
	{
		// 143258B48
		static REL::RelocationID uid(528087, 415032);
		return *((RE::SceneGraph**)uid.address());
	}

	static bool GetVRDrawShadowsDisplay()
	{
		// VR only 141ed3cb0
		static REL::Offset uid{ 0x1ed3cb0 };
		return *((bool*)uid.address());
	}

	static bool GetVRbAccumulateShadowMapsFirst()
	{
		// VR only 141ed3cb0
		static REL::Offset uid{ 0x1ed4118 };
		return *((bool*)uid.address());
	}

	static void ApplyAccmulateShadowMaps2(RE::BSLight* light)
	{
		// VR only 141357450
		using func_t = decltype(&ApplyAccmulateShadowMaps2);
		static REL::Relocation<func_t> func{ REL::Offset(0x1357450) };
		func(light);
	}

	static void VRPrepareShadowMaps(RE::BSLight* light)
	{
		// VR only 141356e50 - manages shadow map resources (ref counting per descriptor)
		using func_t = decltype(&VRPrepareShadowMaps);
		static REL::Relocation<func_t> func{ REL::Offset(0x1356e50) };
		func(light);
	}

	static bool GetUnknownSunBool1()
	{
		// 141E33EB3
		static REL::RelocationID uid(513201, 390932);
		return *((bool*)uid.address());
	}

	static int GetUnknownSunInt1()
	{
		// 1431F6648
		static REL::RelocationID uid(527703, 414625);
		return *((int*)uid.address());
	}

	static bool GetUnknownSunBool2()
	{
		// 143258B71
		static REL::RelocationID uid(528095, 415040);
		return *((bool*)uid.address());
	}

	static bool* GetSelectedFocusShadows()
	{
		// 143258B72
		static REL::RelocationID uid(528096, 415041);
		return (bool*)uid.address();
	}

	static uint64_t* GetUnknownSunPointer1()
	{
		// 1432A9210
		static REL::RelocationID uid(528315, 415267);
		return (uint64_t*)uid.address();
	}

	static uint32_t* GetLastFrameActiveShadowCasterLightCount1()
	{
		// 143258B60
		static REL::RelocationID uid(528090, 415035);
		return (uint32_t*)uid.address();
	}

	static uint32_t* GetLastFrameActiveShadowCasterLightCount2()
	{
		// 143258B64
		static REL::RelocationID uid(528091, 415036);
		return (uint32_t*)uid.address();
	}

	static uint32_t* GetLastFrameActiveShadowCasterLightCount3()
	{
		// 143258B68
		static REL::RelocationID uid(528091, 415036);
		return (uint32_t*)(uid.address() + 4);
	}

	static void ApplyLensFlare(RE::BSLight* light)
	{
		// 1412FC470
		using func_t = decltype(&ApplyLensFlare);
		static REL::Relocation<func_t> func{ REL::RelocationID(100440, 107157) };  // No VR equivalent
		func(light);
	}

	static void unk_Accumulate(RE::BSShadowLight* light)
	{
		// 14131C890
		using func_t = decltype(&unk_Accumulate);
		static REL::Relocation<func_t> func{ REL::RelocationID(100819, 107603) };
		func(light);
	}

	static uint32_t* GetActiveShadowCasterLightMask()
	{
		// 143258B6C
		static REL::RelocationID uid(528093, 415038);
		return (uint32_t*)uid.address();
	}

	static void BSPortalGraphEntry_ClearVisibility(RE::BSPortalGraphEntry* entry)
	{
		// SE 140D3D920 / VR 140d86b70 - clears visibilityMap and releases multiBoundRoomRoot
		using func_t = decltype(&BSPortalGraphEntry_ClearVisibility);
		static REL::Relocation<func_t> func{ REL::RelocationID(74395, 76119) };
		func(entry);
	}

	static bool BSPortalGraphEntry_HasSharedVisibility(RE::BSPortalGraphEntry* first, RE::BSPortalGraphEntry* second)
	{
		// SE 140D3DA20 / VR 140d86c70 - true if entries share visible rooms or both have visibleUnboundSpace
		using func_t = decltype(&BSPortalGraphEntry_HasSharedVisibility);
		static REL::Relocation<func_t> func{ REL::RelocationID(74397, 76121) };
		return func(first, second);
	}

	static RE::BSCullingProcess* GetUnknownGlobalCullingProcess()
	{
		// 143258B00
		static REL::RelocationID uid(528077, 415022);
		return **((RE::BSCullingProcess***)uid.address());
	}

	static void unk_BSShadowDirectionalLight_set(RE::BSShadowLight* light, RE::NiCamera* camera)
	{
		// 14131C170
		using func_t = decltype(&unk_BSShadowDirectionalLight_set);
		static REL::Relocation<func_t> func{ REL::RelocationID(100817, 107601) };
		func(light, camera);
	}

	static void ShadowSceneNode_unk_EnableLight(RE::ShadowSceneNode* shadowSceneNode, RE::BSLight* light)
	{
		// 1412D2070
		using func_t = decltype(&ShadowSceneNode_unk_EnableLight);
		static REL::Relocation<func_t> func{ REL::RelocationID(99708, 106342) };
		func(shadowSceneNode, light);
	}

	static void BSLight_ClearGeometryList(RE::BSLight* light)
	{
		// 141334360
		using func_t = decltype(&BSLight_ClearGeometryList);
		static REL::Relocation<func_t> func{ REL::RelocationID(101298, 108285) };
		func(light);
	}

	static void ShadowSceneNode_SetShadowCasterLightArrayEntry(RE::ShadowSceneNode* shadowSceneNode, RE::BSLight* light, uint32_t index, uint32_t unk4)
	{
		// 1412D3B40
		using func_t = decltype(&ShadowSceneNode_SetShadowCasterLightArrayEntry);
		static REL::Relocation<func_t> func{ REL::RelocationID(99728, 106365) };
		func(shadowSceneNode, light, index, unk4);
	}

	static int GetUnkView(int index)
	{
		// 143052D18
		// 143052D1C

		static REL::RelocationID uid1(524978, 411459);
		static REL::RelocationID uid2(524979, 411460);

		if (index == 0)
			return *((int*)uid1.address());
		return *((int*)uid2.address());
	}

	static float GetVRDRSWidthRatio()
	{
		static REL::Offset bDisableDRS{ 0x3186d28 };
		if (*((int*)bDisableDRS.address()) != 0)
			return 1.0f;
		static REL::Offset ratioUid{ 0x3186d14 };
		return *((float*)ratioUid.address());
	}

	static float GetVRDRSHeightRatio()
	{
		static REL::Offset bDisableDRS{ 0x3186d28 };
		if (*((int*)bDisableDRS.address()) != 0)
			return 1.0f;
		static REL::Offset ratioUid{ 0x3186d18 };
		return *((float*)ratioUid.address());
	}

	static void NiCamera_unk_CalculateFrustumOverlap(RE::NiCamera* camera, float* coord, float* result1, float* result2, float epsilon)
	{
		// 140C65760
		// The binary uses the same address in VR and non‑VR builds, but the
		// prototype differs.  Non‑VR omits the eye index parameter, whereas VR
		// adds a uint32_t before the epsilon.  Dispatch at runtime instead of
		// duplicating the relocation.
		//
		// VR eye index: 0xffffffff (-1 as signed) → use combined ViewFrustum (both eyes).
		// Passing a non-negative index (0=left, 1=right) selects one per-eye frustum,
		// which causes lights near the boundary to flicker with small HMD movements.

		static REL::Relocation<std::uintptr_t> addr{ REL::RelocationID(69265, 70632) };
		auto                                   ptr = addr.address();
		if (REL::Module::IsVR()) {
			using vr_t = void (*)(RE::NiCamera*, float*, float*, float*, std::uint32_t, float);
			auto func = reinterpret_cast<vr_t>(ptr);
			func(camera, coord, result1, result2, 0xffffffff /*combined frustum, both eyes*/, epsilon);
		} else {
			using nonvr_t = void (*)(RE::NiCamera*, float*, float*, float*, float);
			auto func = reinterpret_cast<nonvr_t>(ptr);
			func(camera, coord, result1, result2, epsilon);
		}
	}

	static bool BSLightingShaderProperty_IsLightAffectingSurface(RE::BSLightingShaderProperty* p, RE::BSLight* light)
	{
		// 1412A9410
		using func_t = decltype(&BSLightingShaderProperty_IsLightAffectingSurface);
		static REL::Relocation<func_t> func{ REL::RelocationID(98902, 105550) };
		return func(p, light);
	}

	static int addFrameConvert(RE::BSShadowLight* light, RE::NiCamera* camera, RE::ShadowSceneNode* shadowSceneNode, bool ignoreLimit)
	{
		if (!light)
			return -1;

		int c = 0;
		for (auto& l : g_normalConvert) {
			if (l.light == light) {
				OnDecidedToConvert(light, camera, shadowSceneNode, false);
				return 0;
			} else if (!l.isNS)
				c++;
		}

		if (!ignoreLimit && c >= settings::iMaxConvertCount)
			return -1;

		OnDecidedToConvert(light, camera, shadowSceneNode, true);
		auto& e = g_normalConvert.emplace_back();
		e.light = light;
		e.isNS = ignoreLimit;
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
			for (auto itr = g_normalConvert.begin(); itr != g_normalConvert.end(); itr++) {
				if (itr->light != light)
					continue;

				BSLight_ClearGeometryList(light);
				g_normalConvert.erase(itr);
				break;
			}
		}

		BSPortalGraphEntry_ClearVisibility(light->cullingProcess->portalGraphEntry);
		light->ReturnShadowmaps();
	}

	static void OnDecidedToConvert(RE::BSShadowLight* light, [[maybe_unused]] RE::NiCamera* camera, RE::ShadowSceneNode* shadowSceneNode, bool prepass)
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

			BSPortalGraphEntry_ClearVisibility(portal);
			light->ReturnShadowmaps();
		} else {
			ShadowSceneNode_unk_EnableLight(shadowSceneNode, light);
		}
	}

	static void OnDecidedToEnable(RE::BSShadowLight* light, RE::NiCamera* camera, RE::ShadowSceneNode* shadowSceneNode, int doneLightCount, bool isForCs, bool isSun, bool shadow)
	{
		if (!light)
			SKSE::stl::report_and_fail("Called OnDecidedToEnable(...) on a null light in " PLUGIN_NAME "!");

		if (settings::bTryNormalLight) {
			for (auto itr = g_normalConvert.begin(); itr != g_normalConvert.end(); itr++) {
				if (itr->light == light) {
					BSLight_ClearGeometryList(light);
					g_normalConvert.erase(itr);
					break;
				}
			}
		}

		if (!isForCs) {
			if (!light->UpdateCamera(camera))
				SKSE::stl::report_and_fail("OnDecidedToEnable(...) UpdateCamera call failed on light in " PLUGIN_NAME "!");
		}

		// Extended mode (iCSDebugLightCount > 4): parabolic lights use kSHADOWMAPS slots 4+
		// (via GetBufferIndexForLight), conflicting with g_focusShadowBaseSlotIndex=4. Skip
		// focus shadows entirely. In VR, also reset vrRenderTarget[0/1] to kNONE after
		// unk_Accumulate: RenderCascade sets bVRStereoCopy when GetIsDirectionalLight()&&
		// vrRenderTarget[0/1]!=kNONE; zero-init (0) != kNONE (0xFFFFFFFF) triggers starburst.
		if (settings::iCSDebugLightCount <= 4) {
			if (GetShadowLightDrawFocusShadows(light) || (!*GetSelectedFocusShadows() && light->GetIsFrustumOrDirectionalLight())) {
				unk_BSShadowDirectionalLight_set(light, camera);
				unk_Accumulate(light);
				if (REL::Module::IsVR()) {
					auto& vrd = light->GetVRRuntimeData();
					for (auto& desc : vrd.focusShadowmapDescriptors) {
						desc.vrRenderTarget[0] = RE::RENDER_TARGET_DEPTHSTENCIL::kNONE;
						desc.vrRenderTarget[1] = RE::RENDER_TARGET_DEPTHSTENCIL::kNONE;
					}
				}
				SetShadowLightDrawFocusShadows(light, true);
				*GetSelectedFocusShadows() = true;
				*GetUnknownSunPointer1() = (uint64_t)light;
			}
		}

		if (!isSun && shadow) {
			ShadowSceneNode_unk_EnableLight(shadowSceneNode, light);
			ShadowSceneNode_SetShadowCasterLightArrayEntry(shadowSceneNode, light, *GetLastFrameActiveShadowCasterLightCount2(), 1);
			{
				uint32_t tmp = *GetLastFrameActiveShadowCasterLightCount3();
				SetShadowLightMaskIndex(light, tmp);
				tmp++;
				*GetLastFrameActiveShadowCasterLightCount3() = tmp;
			}

			if (!isSun) {
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
				if (v22 >= nilight->GetLightRuntimeData().radius.x + camera->GetNearPlane()) {
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
				float v27_base = (float)GetUnkView(0);
				float v28_base = (float)GetUnkView(1);
				if (REL::Module::IsVR()) {
					v27_base *= GetVRDRSWidthRatio();
					v28_base *= GetVRDRSHeightRatio();
				}
				v27 = v27_base;
				v28 = v28_base;
				float left = (v24 + 1.0f) * 0.5f * v27;
				float right = (v26 + 1.0f) * 0.5f * v27;
				float top = (1.0f - ((v23 + 1.0f) * 0.5f)) * v28;
				float bottom = (1.0f - ((v25 + 1.0f) * 0.5f)) * v28;
				SetShadowLightProjectedBoundingBox(light, RE::NiRect<std::uint32_t>((uint32_t)left, (uint32_t)right, (uint32_t)top, (uint32_t)bottom));
				if (isForCs) {
					uint32_t tmp_i = (uint32_t)doneLightCount;
					light->Accumulate(tmp_i, (uint32_t)doneLightCount, nullptr);
					*GetLastFrameActiveShadowCasterLightCount2() += light->shadowMapCount;
				} else
					light->Accumulate(*GetLastFrameActiveShadowCasterLightCount2(), static_cast<std::uint32_t>(doneLightCount), nullptr);
				// Accumulate sets renderTarget=NiLight+0x150 (non-kNONE), causing RenderCascade to
				// skip the index-selection block and use shadowmapIndex=0 for every light, so all
				// shadow faces corrupt slot 0. Pre-set renderTarget=kNONE and shadowmapIndex so
				// each light renders into its own slot. In VR, BSShadowParabolicLight::Render
				// copies desc[0].renderTarget into desc[1].renderTarget before face-1 RenderCascade;
				// pre-setting shadowmapIndex prevents face-1 from reusing slot 0.
				if (settings::iCSDebugLightCount > 4) {
					int32_t idx = GetBufferIndexForLight(light);
					if (REL::Module::IsVR()) {
						auto& descs = light->GetVRRuntimeData().shadowmapDescriptors;
						for (auto& desc : descs) {
							desc.renderTarget = RE::RENDER_TARGET_DEPTHSTENCIL::kNONE;
							desc.shadowmapIndex = static_cast<std::uint32_t>(idx);
						}
					} else {
						auto& descs = light->GetRuntimeData().shadowmapDescriptors;
						for (auto& desc : descs) {
							desc.renderTarget = RE::RENDER_TARGET_DEPTHSTENCIL::kNONE;
							desc.shadowmapIndex = static_cast<std::uint32_t>(idx);
						}
					}
				}
				if (light->lensFlareData && !REL::Module::IsVR())
					ApplyLensFlare(light);
			}
		}
	}

// helper macro for VR/SE runtime data access (only use when the field
// type is identical in both runtime-data structs; shadowmapDescriptors
// has a different element type in VR vs. non‑VR and cannot be used here)
#define SHADOW_FIELD(light, member) \
	(REL::Module::IsVR() ? (light)->GetVRRuntimeData().member : (light)->GetRuntimeData().member)

	static RE::BSCullingProcess* GetShadowLightCullingProcess(RE::BSShadowLight* light)
	{
		// shadowmapDescriptors has different element types, so avoid the macro
		if (REL::Module::IsVR())
			return light->GetVRRuntimeData().shadowmapDescriptors.front().cullingProcess;
		else
			return light->GetRuntimeData().shadowmapDescriptors.front().cullingProcess;
	}

	static bool GetShadowLightDrawFocusShadows(RE::BSShadowLight* light)
	{
		return SHADOW_FIELD(light, drawFocusShadows);
	}

	static void SetShadowLightDrawFocusShadows(RE::BSShadowLight* light, bool value)
	{
		SHADOW_FIELD(light, drawFocusShadows) = value;
	}

	static void SetShadowLightMaskIndex(RE::BSShadowLight* light, uint32_t value)
	{
		SHADOW_FIELD(light, maskIndex) = value;
	}

	static void SetShadowLightProjectedBoundingBox(RE::BSShadowLight* light, RE::NiRect<std::uint32_t> rect)
	{
		SHADOW_FIELD(light, projectedBoundingBox) = rect;
	}

	struct _tmp_l
	{
		_tmp_l()
		{
		}

		RE::BSShadowLight* bslight{ nullptr };
		double             score{ 0.0 };
		double             allowConvert{ 0.0 };
		double             allowConvert2{ 0.0 };
		bool               isNS{ false };
		bool               sun{ false };
		uint8_t            chosen{ 0 };
		bool               want{ false };
		bool               redraw{ false };
	};

	static void CalculateActiveShadowCasterLights()
	{
		auto shadowSceneNode = GetShadowSceneNode();
		auto worldSceneGraph = GetWorldSceneGraph();
		auto worldCamera = ((RE::BSSceneGraph*)worldSceneGraph)->GetRuntimeData().camera.get();

		bool     sunBool1 = GetUnknownSunBool1() && GetUnknownSunInt1();
		int      doneLightCount = 0;
		uint64_t isSelectedSun = 0;

		if (!REL::Module::IsVR() || GetVRDrawShadowsDisplay()) {  // VR only check
			if (!GetUnknownSunBool2()) {
				auto sun = shadowSceneNode->GetRuntimeData().sunShadowDirLight;
				if (sun) {
					// VR: a_vrUpdateFlag = 1 + shadowUpdateFlag (set by ResetCalculatedShadowCasterLights); SE/AE: ignores extra param anyway
					static REL::Relocation<bool*> g_vrShadowUpdateFlag{ REL::Offset(0x1ed62f8) };
					std::uint8_t                  vrFlag = REL::Module::IsVR() ? static_cast<std::uint8_t>(*g_vrShadowUpdateFlag) + 1 : 0;
					sun->Accumulate(*GetLastFrameActiveShadowCasterLightCount2(), 0, nullptr, vrFlag);

					if (sunBool1 && settings::iCSDebugLightCount <= 4) {
						unk_Accumulate(sun);
						if (REL::Module::IsVR()) {
							auto& vrd = sun->GetVRRuntimeData();
							for (auto& desc : vrd.focusShadowmapDescriptors) {
								desc.vrRenderTarget[0] = RE::RENDER_TARGET_DEPTHSTENCIL::kNONE;
								desc.vrRenderTarget[1] = RE::RENDER_TARGET_DEPTHSTENCIL::kNONE;
							}
						}
						SetShadowLightDrawFocusShadows(sun, true);
						*GetSelectedFocusShadows() = true;
						*GetUnknownSunPointer1() = 0;
					}

					if (sun->lensFlareData && !REL::Module::IsVR())
						ApplyLensFlare(sun);
					if (REL::Module::IsVR() && !GetVRbAccumulateShadowMapsFirst()) {
						VRPrepareShadowMaps(sun);
						ApplyAccmulateShadowMaps2(sun);
					}
					doneLightCount = 1;
					isSelectedSun = (uint64_t)sun;
				}
			}

			if (sunBool1 && !*GetSelectedFocusShadows() && settings::iCSDebugLightCount <= 4) {
				for (auto itr = shadowSceneNode->GetRuntimeData().activeShadowLights.begin(); itr != shadowSceneNode->GetRuntimeData().activeShadowLights.end(); itr++) {
					auto l = itr->get();
					if (!l)
						continue;

					if ((uint64_t)l == *GetUnknownSunPointer1() && !*GetSelectedFocusShadows()) {
						SetShadowLightDrawFocusShadows(l, true);
						*GetSelectedFocusShadows() = true;
					} else
						SetShadowLightDrawFocusShadows(l, false);
				}
			}
			// Extended mode: ensure drawFocusShadows is cleared for all lights to prevent
			// stale flags from triggering the focus shadow render loop in BSShadowParabolicLight::Render
			if (settings::iCSDebugLightCount > 4) {
				for (auto itr = shadowSceneNode->GetRuntimeData().activeShadowLights.begin(); itr != shadowSceneNode->GetRuntimeData().activeShadowLights.end(); itr++) {
					auto l = itr->get();
					if (l)
						SetShadowLightDrawFocusShadows(l, false);
				}
				auto sun2 = shadowSceneNode->GetRuntimeData().sunShadowDirLight;
				if (sun2)
					SetShadowLightDrawFocusShadows(sun2, false);
			}

			*GetUnknownSunPointer1() = 0;

			auto* data = &get()->_gamedata;

			//clearFrameConvert();

			int convertedToShadow = 0;

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
					e.isNS = FormulaHelper::GetParam(kFormulaParam_LightNS) >= 0.5;
					e.allowConvert = 1.0;
					e.allowConvert2 = 1.0;
					if (g_formulaAllowConvert)
						e.allowConvert = g_formulaAllowConvert->Calculate();
					if (e.isNS && g_formulaAllowConvertShadow)
						e.allowConvert2 = g_formulaAllowConvertShadow->Calculate();
				}
				std::sort(vec.begin(), vec.end(), _SortFunc);

				for (int i = 0; i < g_lastFrameChosenCount; i++)
					g_lastFrameChosenBuf[i] = 0;
				g_lastFrameChosenCount = 0;

				if (data && data->DebugCurrentSCLight)
					data->DebugCurrentSCLight->value = (float)(doneLightCount + (int32_t)vec.size());

				int debugConvert = 0;
				if (data && data->DebugForceConvert)
					debugConvert = (int)data->DebugForceConvert->value;

				for (auto itr = vec.begin(); itr != vec.end(); itr++) {
					auto                    l = itr->bslight;
					RE::BSCullingProcess*   cull;
					RE::BSPortalGraphEntry* portal;
					if (doneLightCount < settings::iLightCount && debugConvert <= 0 && (!itr->isNS || (itr->allowConvert2 >= 0.5 && convertedToShadow < settings::iMaxConvertCountShadow))) {
						if (l->UpdateCamera(worldCamera) && (cull = GetShadowLightCullingProcess(l)) != nullptr && (portal = cull->portalGraphEntry) != nullptr && BSPortalGraphEntry_HasSharedVisibility(GetUnknownGlobalCullingProcess()->portalGraphEntry, portal)) {
							OnDecidedToEnable(l, worldCamera, shadowSceneNode, doneLightCount, false, false, true);
							doneLightCount++;

							if (itr->isNS)
								convertedToShadow++;

							if (g_lastFrameChosenCount < 4)
								g_lastFrameChosenBuf[g_lastFrameChosenCount++] = (uint64_t)l;
						} else
							OnDecidedToDisable(l);
					} else if (itr->isNS || (settings::bTryNormalLight && debugConvert >= 0 && itr->allowConvert >= 0.5)) {
						if (l->UpdateCamera(worldCamera) && (cull = GetShadowLightCullingProcess(l)) != nullptr && (portal = cull->portalGraphEntry) != nullptr && BSPortalGraphEntry_HasSharedVisibility(GetUnknownGlobalCullingProcess()->portalGraphEntry, portal)) {
							int converted = addFrameConvert(l, worldCamera, shadowSceneNode, itr->isNS);
							if (converted >= 0) {
								// this is now done in addFrameConvert
								//OnDecidedToConvert(l, worldCamera, shadowSceneNode);
							} else
								OnDecidedToDisable(l);
						} else
							OnDecidedToDisable(l);
					} else {
						OnDecidedToDisable(l);
					}
				}

				if (data && data->DebugActiveSCLight)
					data->DebugActiveSCLight->value = (float)(doneLightCount);
			}

			if (data && data->DebugData) {
				int c = 0;
				for (auto& x : g_normalConvert) {
					if (!x.isNS)
						c++;
				}
				data->DebugData->value = (float)c;
			}

			if (data && data->DebugData2)
				data->DebugData2->value = (float)convertedToShadow;

			shadowSceneNode->GetRuntimeData().firstPersonShadowMask = *GetActiveShadowCasterLightMask();
			*GetLastFrameActiveShadowCasterLightCount1() = (uint32_t)doneLightCount;
		}
	}

	static void ShadowSceneNode_ClearShadowCasterLightArray(RE::ShadowSceneNode* shadowSceneNode)
	{
		// 1412D3BC0
		using func_t = decltype(&ShadowSceneNode_ClearShadowCasterLightArray);
		static REL::Relocation<func_t> func{ REL::RelocationID(99730, 106367) };
		func(shadowSceneNode);
	}

	static void CalculateActiveShadowCasterLights_CS()
	{
		auto shadowSceneNode = GetShadowSceneNode();
		auto worldSceneGraph = GetWorldSceneGraph();
		auto worldCamera = ((RE::BSSceneGraph*)worldSceneGraph)->GetRuntimeData().camera.get();

#ifdef L_MEASURE_PERFORMANCE
		g_perf.beginLight(nullptr, 0);
#endif

		g_budget.Begin(0);

		std::vector<_tmp_l> all;

		int                doneLightCount = 0;
		RE::BSShadowLight* isSun = nullptr;

		if (!GetUnknownSunBool2()) {
			auto sun = shadowSceneNode->GetRuntimeData().sunShadowDirLight;
			if (sun) {
				auto& bk = all.emplace_back();
				bk.sun = true;
				bk.isNS = false;
				bk.bslight = sun;
				bk.want = true;
				bk.chosen = 1;

				isSun = sun;

				// VR: a_vrUpdateFlag = 1 + shadowUpdateFlag (mirrors non-CS path)
				static REL::Relocation<bool*> g_vrShadowUpdateFlag{ REL::Offset(0x1ed62f8) };
				std::uint8_t                  vrFlag = REL::Module::IsVR() ? static_cast<std::uint8_t>(*g_vrShadowUpdateFlag) + 1 : 0;
				sun->Accumulate(*GetLastFrameActiveShadowCasterLightCount2(), 0, nullptr, vrFlag);

				if (sun->lensFlareData && !REL::Module::IsVR())
					ApplyLensFlare(sun);
				if (REL::Module::IsVR() && !GetVRbAccumulateShadowMapsFirst()) {
					VRPrepareShadowMaps(sun);
					ApplyAccmulateShadowMaps2(sun);
				}
			}
		}

#ifdef PLUGIN_DEBUG_FRAME
		if (IsDebugFrame()) {
			auto sunl = shadowSceneNode->GetRuntimeData().shadowCasterLights[0];
			logs::info("dbgsun; accum: {}; dirlight: {}", (int64_t)sunl, (int64_t)isSun);
		}
#endif

		*GetUnknownSunPointer1() = 0;

		auto* data = &get()->_gamedata;

		int wantCount = 0;

		if (shadowSceneNode->GetRuntimeData().activeShadowLights.size() > 0) {
			SetupSceneFormula(worldCamera, shadowSceneNode);

			int32_t tmpIndex = 0;
			for (auto itr = shadowSceneNode->GetRuntimeData().activeShadowLights.begin(); itr != shadowSceneNode->GetRuntimeData().activeShadowLights.end(); itr++) {
				auto l = itr->get();
				if (!l || isSun == l)
					continue;

				auto& e = all.emplace_back();
				e.sun = isSun == l;
				e.bslight = l;
				if (!e.sun) {
					e.score = CalculateLightScore(l, worldCamera, tmpIndex++, shadowSceneNode);
					e.isNS = FormulaHelper::GetParam(kFormulaParam_LightNS) >= 0.5;
					e.allowConvert = 1.0;
					e.allowConvert2 = 1.0;
					if (g_formulaAllowConvert)
						e.allowConvert = g_formulaAllowConvert->Calculate();
					if (e.isNS && g_formulaAllowConvertShadow)
						e.allowConvert2 = g_formulaAllowConvertShadow->Calculate();
				}
			}
			std::sort(all.begin(), all.end(), _SortFunc);

			for (int i = 0; i < g_lastFrameChosenCount; i++)
				g_lastFrameChosenBuf[i] = 0;
			g_lastFrameChosenCount = 0;

			for (auto itr = all.begin(); itr != all.end(); itr++) {
				auto                    l = itr->bslight;
				RE::BSCullingProcess*   cull;
				RE::BSPortalGraphEntry* portal;
				if (isSun == l || (l->UpdateCamera(worldCamera) && (cull = GetShadowLightCullingProcess(l)) != nullptr && (portal = cull->portalGraphEntry) != nullptr && BSPortalGraphEntry_HasSharedVisibility(GetUnknownGlobalCullingProcess()->portalGraphEntry, portal))) {
					itr->want = true;

					if (wantCount >= settings::iCSDebugLightCount) {
						OnDecidedToDisable(l);
					} else {
						itr->chosen = 1;

						if (g_lastFrameChosenCount < 4)
							g_lastFrameChosenBuf[g_lastFrameChosenCount++] = (uint64_t)l;
					}

					wantCount++;
				} else
					OnDecidedToDisable(l);
			}
		}

		// Free previous lights that we didn't select anymore
		for (int i = 0; i < g_lightsAll.Size; i++) {
			if (g_lightsAll.Lights[i].Light) {
				uint8_t thisChoose = i < settings::iCSDebugLightCount ? 1 : 2;
				uint8_t didChoose = 0;
				for (auto& x : all) {
					if (x.bslight == g_lightsAll.Lights[i].Light) {
						didChoose = x.chosen;
						break;
					}
				}

				// We can have a OnUnselectLight here if needed or something
				if (didChoose != thisChoose) {
					g_lightsAll.Lights[i].ClearLight();
					if (i == 0)
						g_lightsAll.Sun = false;
				}
			}
		}

		// Add new lights that we didn't select previously
		for (auto& x : all) {
			if (x.chosen == 0)
				continue;

			if (x.sun) {
				if (g_lightsAll.Lights[0].Light != x.bslight) {
					g_lightsAll.Lights[0].ClearLight();
					g_lightsAll.Lights[0].Light = x.bslight;
					g_lightsAll.Sun = true;
				}
			}
		}
		for (auto& x : all) {
			if (x.chosen == 0 || x.sun)
				continue;

			bool alreadyChosen = false;
			for (int i = 0; i < g_lightsAll.Size; i++) {
				if (g_lightsAll.Lights[i].Light == x.bslight) {
					alreadyChosen = true;
					break;
				}
			}

			if (alreadyChosen)
				continue;

			int freeIndex = g_lightsAll.FindFreeIndex(x.chosen == 1);
			if (freeIndex < 0) {
				// This should never happen!
				OnDecidedToDisable(x.bslight);
				continue;
			}

			g_lightsAll.Lights[freeIndex].Light = x.bslight;
			// OnSelectedLight
		}

		// Decide which lights to draw this frame
		{
			int maxCan = settings::iMaxRedrawLightPerFrame;

			double  budget = 2.0;
			int32_t budgetRemain = (int32_t)(budget * 1000.0);
			bool    isFirst = true;
			int     now = GetCurrentGameFrameCounter();

			for (int i = settings::iCSDebugLightCount; i < g_lightsAll.Size; i++)
				g_lightsAll.Lights[i].RedrawFrame = false;

			for (int i = 0; i < g_lightsAll.Size; i++) {
				auto& l = g_lightsAll.Lights[i];
				if (!l.Light) {
					l.RedrawFrame = false;
					continue;
				}

				l.RedrawFrame = (i == 0 && g_lightsAll.Sun) || (l.LastDrawnFrame < 0 && settings::bForceAllowDrawNewLight);
				if (l.RedrawFrame) {
					l.LastDrawnFrame = now;
					isFirst = false;
					maxCan--;

					if (i != 0 || !g_lightsAll.Sun) {
						int32_t estimatedBudget = g_budget.GetBudget(l.Light);
						budgetRemain -= estimatedBudget;
					}
				}
			}

			if (maxCan > 0 && budgetRemain > 0) {
				std::vector<c_light_entry*> vec;
				for (int i = 0; i < g_lightsAll.Size; i++) {
					auto& l = g_lightsAll.Lights[i];
					if (!l.Light || l.RedrawFrame)
						continue;

					vec.push_back(&l);
				}

				if (g_formulaRedrawBudget)
					budget = g_formulaRedrawBudget->Calculate();

				for (auto e : vec) {
					double interval = 0.0;
					if (g_formulaRenderInterval) {
						SetupLightFormula(e->Light, worldCamera, shadowSceneNode, 0);
						if (e->Index >= settings::iCSDebugLightCount)
							FormulaHelper::SetParam(kFormulaParam_LightConverted, 1.0);
						interval = g_formulaRenderInterval->Calculate();
					}
					interval += 1.0;

					e->RedrawScore = e->LastDrawnFrame + interval;
				}

				std::sort(vec.begin(), vec.end(), _SortFunc2);

				for (auto e : vec) {
					if (maxCan <= 0)
						break;

					if (budgetRemain <= 0)
						break;

					int32_t budgetEstimate = g_budget.GetBudget(e->Light);

					// First light in the list should always be allowed to draw, because otherwise we can get into a situation where the light exceeds budget and is never going to draw
					if (isFirst) {
						if (!g_lightsAll.Sun || e->Index > 0)
							budgetRemain -= budgetEstimate;
						maxCan--;

						e->RedrawFrame = true;
						e->LastDrawnFrame = now;
						isFirst = false;

						continue;
					}

					// This light fits into the remaining buffer
					if (budgetEstimate <= budgetRemain) {
						budgetRemain -= budgetEstimate;
						maxCan--;

						e->RedrawFrame = true;
						e->LastDrawnFrame = now;

						continue;
					}

					// This light does not fit into the budget and will not be redrawn
				}
			}
		}

		// Actually activate the lights now
		for (int i = 0; i < g_lightsAll.Size; i++) {
			auto& l = g_lightsAll.Lights[i];
			if (l.Light) {
				if (l.RedrawFrame && i < settings::iCSDebugLightCount) {
					bool sun = i == 0 && g_lightsAll.Sun;
					if (!sun)
						l.Light->UpdateCamera(worldCamera);
#ifdef L_MEASURE_PERFORMANCE
					g_perf.beginLight(l.Light, 0);
#endif
					g_budget.BeginLight(l.Light, 0);
#ifdef USE_ACCUM_SET
					l.Accum->clear();
#endif
					OnDecidedToEnable(l.Light, worldCamera, shadowSceneNode, i, true, sun, true);
					SetShadowLightMaskIndex(l.Light, (uint32_t)i);
					g_budget.EndLight(l.Light, 0);
#ifdef L_MEASURE_PERFORMANCE
					g_perf.endLight(l.Light, 0);
#endif
					doneLightCount++;
				} else {
					OnDecidedToDisable(l.Light);
				}
			}
		}

		int endIndex = 0;
		//uint32_t maskIndex = 0;
		while (true) {
			auto l = shadowSceneNode->GetRuntimeData().shadowCasterLights[endIndex];
			if (!l)
				break;

			endIndex += l->shadowMapCount;
		}

		for (int i = 0; i < g_lightsAll.Size; i++) {
			auto& l = g_lightsAll.Lights[i];
			if (l.Light && (!l.RedrawFrame || i >= settings::iCSDebugLightCount)) {
				ShadowSceneNode_SetShadowCasterLightArrayEntry(shadowSceneNode, l.Light, endIndex, 1);
				endIndex += l.Light->shadowMapCount;
				SetShadowLightMaskIndex(l.Light, (uint32_t)i);
			}
		}

		if (data && data->DebugCurrentSCLight)
			data->DebugCurrentSCLight->value = (float)wantCount;

		if (data && data->DebugActiveSCLight)
			data->DebugActiveSCLight->value = (float)(doneLightCount);

		if (data && data->DebugData) {
			int c = 0;
			for (auto& x : g_normalConvert) {
				if (!x.isNS)
					c++;
			}
			data->DebugData->value = (float)c;
		}

#ifdef PLUGIN_DEBUG_FRAME
		if (IsDebugFrame()) {
			static bool print_perf_once = false;
			if (!print_perf_once) {
				print_perf_once = true;

#	ifdef L_MEASURE_PERFORMANCE
				g_perf.printAll();
#	endif
				g_budget.PrintDebug();
			}

			logs::info("Frame == [{}] ==", GetCurrentGameFrameCounter());
			for (int i = 0; i < g_lightsAll.Size; i++)
				logs::info("> Light[{}]: {:X}, redrawing: {}", i, (int64_t)g_lightsAll.Lights[i].Light, g_lightsAll.Lights[i].RedrawFrame ? 1 : 0);
		}
#endif

		shadowSceneNode->GetRuntimeData().firstPersonShadowMask = *GetActiveShadowCasterLightMask();
		*GetLastFrameActiveShadowCasterLightCount1() = (uint32_t)doneLightCount;

		g_budget.End(0);

#ifdef L_MEASURE_PERFORMANCE
		g_perf.endLight(nullptr, 0);
#endif
	}

	static bool _SortFunc(const _tmp_l& first, const _tmp_l& second)
	{
		if (first.sun && !second.sun)
			return true;

		if (!first.sun && second.sun)
			return false;

		return first.score > second.score;
	}

	static void SetupSceneFormula(RE::NiCamera* camera, [[maybe_unused]] RE::ShadowSceneNode* shadowSceneNode)
	{
		if (camera) {
			FormulaHelper::SetParam(FormulaParams::kFormulaParam_CameraX, camera->world.translate.x);
			FormulaHelper::SetParam(FormulaParams::kFormulaParam_CameraY, camera->world.translate.y);
			FormulaHelper::SetParam(FormulaParams::kFormulaParam_CameraZ, camera->world.translate.z);
		} else {
			FormulaHelper::SetParam(FormulaParams::kFormulaParam_CameraX, 0.0);
			FormulaHelper::SetParam(FormulaParams::kFormulaParam_CameraY, 0.0);
			FormulaHelper::SetParam(FormulaParams::kFormulaParam_CameraZ, 0.0);
		}

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

	static void SetupLightFormula(RE::BSShadowLight* light, RE::NiCamera* camera, [[maybe_unused]] RE::ShadowSceneNode* shadowSceneNode, int32_t index)
	{
		FormulaHelper::SetParam(FormulaParams::kFormulaParam_LightConverted, 0.0);
		FormulaHelper::SetParam(FormulaParams::kFormulaParam_LightIndex, index);

		double chosenLastFrame = 0.0;
		for (int i = 0; i < g_lastFrameChosenCount; i++) {
			if (g_lastFrameChosenBuf[i] == (uint64_t)light) {
				chosenLastFrame = 1.0;
				break;
			}
		}
		FormulaHelper::SetParam(FormulaParams::kFormulaParam_LightChosenLastFrame, chosenLastFrame);

		FormulaHelper::SetParam(FormulaParams::kFormulaParam_LightNeverFades, light->lodFade ? 0.0 : 1.0);  // this declaration is backwards in commonlib, neverFades false means actually never fades
		FormulaHelper::SetParam(FormulaParams::kFormulaParam_LightPortalStrict, light->portalStrict ? 1.0 : 0.0);

		FormulaHelper::SetParam(FormulaParams::kFormulaParam_LightNS, 0.0);

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

			if (settings::bTryShadowLight)
				FormulaHelper::SetParam(FormulaParams::kFormulaParam_LightNS, g_shadowConvert.find(nilight) != g_shadowConvert.end() ? 1.0 : 0.0);
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

		float camx = camera ? camera->world.translate.x : (float)FormulaHelper::GetParam(FormulaParams::kFormulaParam_CameraX);
		float camy = camera ? camera->world.translate.y : (float)FormulaHelper::GetParam(FormulaParams::kFormulaParam_CameraY);
		float camz = camera ? camera->world.translate.z : (float)FormulaHelper::GetParam(FormulaParams::kFormulaParam_CameraZ);

		float dx = x - camx;
		float dy = y - camy;
		float dz = z - camz;
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
			_addr(100419, 0, REL::Relocate("48 89 6C 24 18", "48 89 6C 24 18", "48 89 6C 24 10")),
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

		for (auto itr = g_normalConvert.begin(); itr != g_normalConvert.end(); itr++) {
			if (itr->light == light) {
				result = false;
				break;
			}
		}

		return result;
	}

	static bool Hook_ConvertLights()
	{
		if (settings::bTryNormalLight) {
			// 14131D2A0
			REL::Relocation<std::uintptr_t> vtbl1{ RE::BSShadowLight::VTABLE[0] };
			vtbl1.write_vfunc(3, BSShadowLight_IsShadowLight);
			REL::Relocation<std::uintptr_t> vtbl2{ RE::BSShadowDirectionalLight::VTABLE[0] };
			vtbl2.write_vfunc(3, BSShadowLight_IsShadowLight);
			REL::Relocation<std::uintptr_t> vtbl3{ RE::BSShadowFrustumLight::VTABLE[0] };
			vtbl3.write_vfunc(3, BSShadowLight_IsShadowLight);
			REL::Relocation<std::uintptr_t> vtbl4{ RE::BSShadowParabolicLight::VTABLE[0] };
			vtbl4.write_vfunc(3, BSShadowLight_IsShadowLight);
		}

		if (settings::bTryNormalLight || settings::bTryShadowLight) {
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

		if (settings::bTryShadowLight || settings::bForcePortalStrict) {
			// SE 1412B9320 / VR 1412F6D50 - ShadowSceneNode::AddLight
			static _addr addr[] = {
				_addr(99692, 0, "4C 8B DC 55 56"),
				_addr(106326, 0, "4C 8B DC 55 56"),
			};

			void* a = get_addr(addr);

			if (!HookHelper::WriteHook(a, 5, 5, _Hook_ConvertLights_Add))
				return false;
		}

		if (settings::bTryShadowLight) {
			{
				// SE 14131D1F0 / VR 141363070 - SetLight; ID 101302 in VR DB
				static _addr addr[] = {
					_addr(101302, 0, "48 89 5C 24 10"),
					_addr(108289, 0, "48 89 5C 24 10"),
				};

				void* a = get_addr(addr);

				if (!HookHelper::WriteHook(a, 5, 5, _Hook_ConvertLights_SetLight))
					return false;
			}
		}

		return true;
	}

	static void _Hook_ConvertLights_Remove(CONTEXT& ctx)
	{
		RE::ShadowSceneNode* shadowSceneNode = (RE::ShadowSceneNode*)ctx.Rcx;
		if (shadowSceneNode != GetShadowSceneNode())
			return;

		RE::NiLight* light = (RE::NiLight*)ctx.Rdx;
		for (auto itr = g_normalConvert.begin(); itr != g_normalConvert.end(); itr++) {
			auto l = itr->light->light.get();
			if (l && l == light) {
				BSLight_ClearGeometryList(itr->light);
				g_normalConvert.erase(itr);
				break;
			}
		}

		if (settings::bTryShadowLight) {
			if (light)
				g_shadowConvert.erase(light);
		}
	}

	static void _Hook_ConvertLights_Add(CONTEXT& ctx)
	{
		RE::ShadowSceneNode* shadowSceneNode = (RE::ShadowSceneNode*)ctx.Rcx;
		if (shadowSceneNode != GetShadowSceneNode())
			return;

		RE::NiLight*                              light = (RE::NiLight*)ctx.Rdx;
		RE::ShadowSceneNode::LIGHT_CREATE_PARAMS* p = (RE::ShadowSceneNode::LIGHT_CREATE_PARAMS*)ctx.R8;

		if (!light || !p)
			return;

		if (settings::bTryShadowLight && !p->shadowLight) {
			p->shadowLight = true;
			p->fov = 6.2831855f;
			p->dynamic = true;
			p->restrictedNode = nullptr;
			p->falloff = 1.0f;
			p->depthBias = 1.0f;                                                             // todo: is this normal?
			p->nearDistance = (light->GetLightRuntimeData().radius.x / 512.0f) * 219.6356f;  // todo: configurable?

			g_shadowConvert.insert(light);
		}

		if (settings::bForcePortalStrict)
			p->portalStrict = true;
	}

	static void _Hook_ConvertLights_SetLight(CONTEXT& ctx)
	{
		RE::BSLight* bslight = (RE::BSLight*)ctx.Rcx;
		RE::NiLight* nilight = (RE::NiLight*)ctx.Rdx;

		if (!bslight)
			return;

		bool did = false;

		auto oldlight = bslight->light.get();
		if (oldlight) {
			if (oldlight == nilight)
				return;

			if (g_shadowConvert.erase(oldlight) != 0)
				did = true;
		}

		if (nilight && did)
			g_shadowConvert.insert(nilight);
	}

	/*static void mby_clear_render_target()
	{
		// 14131FB30
		using func_t = decltype(&mby_clear_render_target);
		static REL::Relocation<func_t> func{ REL::VariantID(100860, 107653, 0) };
		func();
	}

	static RE::BSUtilityShader* GetUtilityShader()
	{
		// 1434BB3E0
		static REL::VariantID uid(528354, 415300, 0);
		return *((RE::BSUtilityShader**)uid.address());
	}

	static RE::BSSpinLock* Get_unk_Mutex()
	{
		// 14131FB30
		using func_t = decltype(&Get_unk_Mutex);
		static REL::Relocation<func_t> func{ REL::VariantID(100719, 107499, 0) };
		return func();
	}

	static RE::BSGeometry* Get_unk_Geometry()
	{
		// 1431F6830
		static REL::VariantID uid(527731, 414660, 0);
		int64_t ptr = *((int64_t*)uid.address());
		ptr += 0x48;
		return *((RE::BSGeometry**)ptr);
	}

	static int* GetCurrentRenderTargetTextureIndex()
	{
		// 14304EF60
		static REL::VariantID uid(524804, 388850, 0);
		return (int*)uid.address();
	}

	static int* GetCurrentRenderTargetOptions()
	{
		// 14304EEB0
		static REL::VariantID uid(524773, 388819, 0);
		return (int*)uid.address();
	}

	static void SetCurrentRenderTargetTextureIndex(int index, bool isShadowMapIndex)
	{
		if (isShadowMapIndex)
		{
			if (index == -4)
				index = 4;
			else
			{
				if (index < 0)
					index = -index;

				index %= 4;
				index = 5 + index;
			}
		}

		if (*GetCurrentRenderTargetTextureIndex() != index)
		{
			*GetCurrentRenderTargetTextureIndex() = index;
			*GetCurrentRenderTargetOptions() |= 0x80;
		}
	}

	static void _tmp_unk_d3d(int64_t a1, int64_t a2, int64_t a3, int64_t a4)
	{
		// 140D74A50
		using func_t = decltype(&_tmp_unk_d3d);
		static REL::Relocation<func_t> func{ REL::VariantID(75647, 77454, 0) };
		return func(a1, a2, a3, a4);
	}

	static int64_t Get_unk_RenderTarget()
	{
		// 143052B20
		static REL::VariantID uid(524970, 411451, 0);
		return (int64_t)uid.address(); // lea rcx
	}

	static void _tmp_unk_d3d_2(int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5)
	{
		// 140D74A30
		using func_t = decltype(&_tmp_unk_d3d_2);
		static REL::Relocation<func_t> func{ REL::VariantID(75646, 77453, 0) };
		return func(a1, a2, a3, a4, a5);
	}

	static bool Get_unk_UtilityBool()
	{
		// 1412FC450
		using func_t = decltype(&Get_unk_UtilityBool);
		static REL::Relocation<func_t> func{ REL::VariantID(100439, 107156, 0) };
		return func();
	}

	static void BSRenderPass_unk(RE::BSRenderPass* pass, int64_t a2, int64_t a3, int64_t a4)
	{
		// 14131F810
		using func_t = decltype(&BSRenderPass_unk);
		static REL::Relocation<func_t> func{ REL::VariantID(100854, 107644, 0) };
		return func(pass, a2, a3, a4);
	}

	static float* Get_unk_MaskIndex()
	{
		// 1432A9208
		static REL::VariantID uid(528313, 415266, 0);
		return (float*)uid.address(); // lea rcx
	}*/

	/*static void RenderShadowLightsWithUtilityShader()
	{
		mby_clear_render_target();
		_tmp_unk_d3d(Get_unk_RenderTarget(), -1, 3, 0);
		_tmp_unk_d3d_2(Get_unk_RenderTarget(), 0, 18, 0, 1);
		for (int i = 0; i < g_cs->activeShadowLightsCount; i++)
		{
			auto l = g_cs->activeShadowLights[i];
			int lightIndex = (int)l->GetRuntimeData().shadowLightIndex;
			if (lightIndex >= 0 && lightIndex < 4)
			{
				auto lock = Get_unk_Mutex();
				lock->Lock();

				auto shader = GetUtilityShader();
				RE::BSRenderPass* pass;

				if (l->IsDirectionalLight())
				{
					// sub_1412FC450() is bool return
					pass = shader->MakeRenderPass(nullptr, Get_unk_Geometry(), ((Get_unk_UtilityBool() ? 0x2100 : 0x2000) | 0x200002u) + 43, 1u, (RE::BSLight**)&l);
				}
				else
				{
					int v8;
					if (l->IsSpotLight())
						v8 = l->GetRuntimeData().drawFocusShadows ? 0x400100 : 0x400000;
					else
						v8 = l->IsOmnidirectionalLight() ? 0x1000000 : 0x800000;
					int64_t unkData = *((int64_t*)((int64_t)l + 0x148));
					int32_t unkInt = *((int32_t*)(unkData + 0x58));
					*Get_unk_MaskIndex() = (float)unkInt;
					pass = shader->MakeRenderPass(nullptr, Get_unk_Geometry(), (v8 | 0x2002u) + 43, 1u, (RE::BSLight**)&l);
				}

				pass->extraParam = 255;
				SetCurrentRenderTargetTextureIndex(lightIndex, true);
				BSRenderPass_unk(pass, pass->passEnum, 0, 0);
				pass->ClearRenderPass();
				mby_clear_render_target();

				if (lock)
					lock->Unlock();
			}
			//v1 += l->shadowMapCount;
			//logs::info("ResetLight({:X})", (int64_t)l);
			l->Reset();
		}
		SetCurrentRenderTargetTextureIndex(1, false);
	}*/

	static bool Hook_Utility()
	{
		if (settings::iCSDebugLightCount <= 0)
			return true;

		// This part actually renders the depth from POV of camera
		/*{
			// 1412FAF50
			static _addr addr[] = {
				_addr(100423, 0, "40 55 56 57 41 56"),
				_addr(107141, 0, "40 55 56 57 41 54"),
			};

			void* a = get_addr(addr);
			void* cave = HookHelper::FindCodeCave(a);
			if (!cave)
				return false;

			HookHelper::WriteAbsoluteJump(cave, reinterpret_cast<void*>(&RenderShadowLightsWithUtilityShader));
			HookHelper::WriteRelJump(a, cave);
		}*/

		// This part renders depth from POV of lights
		/*{
			// 1412FA850
			static _addr addr[] = {
				_addr(100420, 0, "40 57 48 83 EC 30"),
				_addr(107138, 0, "40 57 48 83 EC 30"),
			};

			void* a = get_addr(addr);
			void* cave = HookHelper::FindCodeCave(a);
			if (!cave)
				return false;

			HookHelper::WriteAbsoluteJump(cave, reinterpret_cast<void*>(&RenderActiveShadowCasterLights));
			HookHelper::WriteRelJump(a, cave);
		}*/

		// Allow changing array size of depth stencil buffers (game already makes 8 and only uses 4?)
		/*if (settings::iCSDebugLightCount > 8)
		{
			// 1412FD326
			static _addr addr[] = {
				_addr(100458, 0xD326 - 0xC940, "C7 44 24 68 08 00 00 00"),
				_addr(107175, 0xBF6 - 0x210, "C7 44 24 68 08 00 00 00"),
			};

			void* a = get_addr(addr);
			if (!a)
				return false;

			if (!MemoryHelper::WriteByte(MemoryHelper::AddPointer(a, 4), (uint8_t)settings::iCSDebugLightCount))
				return false;
		}*/

		// Write info about buffer usage for CS
		/*{
			// 14131CD11
			static _addr addr[] = {
				_addr(100820, 0xD11 - 0x9E0, "4C 8D 44 24 78"),
				_addr(107604, 0x101A - 0x0CF0, "4C 8D 44 24 78"),
			};

			void* a = get_addr(addr);
			if (!a || !HookHelper::WriteHook(a, 5, 5, _Hook_BufferUsage))
				return false;
		}*/

		// Don't draw in upper 4 depth buffers (Frustum light)
		/*{
			// SE 14132D062 (BSShadowFrustumLight::Render +0x22)
			static _addr addr[] = {
				_addr(101613, 0x432 - 0x410, "74 78"),
				_addr(108616, 0xAE2 - 0xAC0, "74 78"),
			};

			void* a = get_addr(addr);
			if (!a)
				return false;

			unsigned char write = 0xEB;
			if (!MemoryHelper::WriteByte(a, write))
				return false;
		}

		// Don't draw in upper 4 depth buffers (directional light)
		{
			// 14133C0DC
			static _addr addr[] = {
				_addr(101495, 0xC0DC - 0xC000, "74 5C"),
				_addr(108489, 0xE3C - 0xD60, "74 5C"),
			};

			void* a = get_addr(addr);
			if (!a)
				return false;

			unsigned char write = 0xEB;
			if (!MemoryHelper::WriteByte(a, write))
				return false;
		}*/

		return true;
	}

	// Extend possible active lights for Community Shaders
	static bool Hook_ExtendLights()
	{
		if (!HookEx_AccumulatedLightsArray())
			return false;

		if (!HookEx_ExtendDepthBufferArray())
			return false;

		if (!HookEx_UseMoreDepthBuffers())
			return false;

		if (!HookEx_DisableFocusShadows())
			return false;

		//if (settings::bCSDisableUselessPass)
		{
			if (!HookEx_DisableColorMaskDrawing())
				return false;
		}

		if (!HookEx_CalculateActiveLights())
			return false;

		if (!HookEx_RenderShadowLights())
			return false;

		if (!HookEx_OverwriteShadowMapIndexSelection())
			return false;

		if (!HookEx_SetupRenderPass())
			return false;

		return true;
	}

	static bool HookEx_SetupRenderPass()
	{
		// 14132828B - add more shadow lights to render pass lights
		/*{
			static _addr addr[] = {
				//_addr(100997, 0x286 - 0x1E0, "4C 89 64 24 40 83 C0 08"),
				//_addr(107784, 0xD018 - 0xCF80, "45 32 E4 83 C0 08"),
				_addr(100997, REL::Relocate(0x28B - 0x1E0, 0x28B - 0x1E0, 0xA6), REL::Relocate("83 C0 08", "83 C0 08", "8D 69 08")),
				_addr(107784, 0xD01B - 0xCF80, "83 C0 08"),
			};

			void* a = get_addr(addr);
			if (!a)
				return false;

			if (!MemoryHelper::WriteInt32(MemoryHelper::AddPointer(a, 4), settings::iCSDebugLightCount))
				return false;
		}*/

		// Force consider all lights for surface - temp because i don't know where it calculates active light mask per surface yet
		/*{
			// 1413282E1
			static _addr addr[] = {
				_addr(100997, 0x2E1 - 0x1E0, "76 7A"),
				_addr(107784, 0xD072 - 0xCF80, "76 70"),
			};

			void* a = get_addr(addr);
			if (!a)
				return false;

			if (!HookHelper::WriteNop(a, 2))
				return false;
		}*/

		return true;
	}

	/*static void _HookEx_BSLightingShaderProperty_ctor(CONTEXT& ctx)
	{
		RE::BSShaderPropertyLightData* lightData = (RE::BSShaderPropertyLightData*)ctx.Rdi;
		uint32_t* tmp = (uint32_t*)&lightData->lightListChanged;
		tmp++;
		*tmp = 0;
	}

	static void _HookEx_OnAccumulatingLight(CONTEXT& ctx)
	{
		RE::BSShaderPropertyLightData* lightData = GAME_VER == 0 ? (RE::BSShaderPropertyLightData*)ctx.Rcx : (RE::BSShaderPropertyLightData*)ctx.Rdx;
		RE::BSGraphics::BSShaderAccumulator* accumulator = (RE::BSGraphics::BSShaderAccumulator*)ctx.Rsi;
		int index = (int)(GAME_VER == 0 ? ctx.Rdx : ctx.R8);

		if (index == 0xFFFF)
		{
#ifdef PLUGIN_DEBUG_FRAME
			if (IsDebugFrame())
				logs::info("OnAccum({}, {:X})", index, (uint64_t)lightData);
#endif

			lightData->unk1C = 0;
			uint32_t* tmp = (uint32_t*)&lightData->lightListChanged;
			tmp++;
			*tmp = 0;
		}
		else if(index >= 1 && index <= g_lightsAll.Size)
		{
			index--;
#ifdef PLUGIN_DEBUG_FRAME
			if (IsDebugFrame())
				logs::info("OnAccum({}, {:X})", index, (uint64_t)lightData);
#endif
			uint32_t* tmp = (uint32_t*)&lightData->lightListChanged;
			tmp++;
			uint32_t now = (uint32_t)GetCurrentGameFrameCounter();
			if (now != *tmp)
			{
				*tmp = now;
				lightData->unk1C = 0;
			}

			auto& l = g_lightsAll.Lights[index];
#ifdef USE_ACCUM_SET
			l.Accum->insert((uint64_t)lightData);
#endif

			if(l.DrawIndex >= 0)
				lightData->unk1C |= (uint32_t)1 << l.DrawIndex;
		}
	}*/

	static void _HookEx_CalculateActiveLightsForSurface(CONTEXT& ctx)
	{
		// SE: CalculateActiveNonShadowCasterLights @ ID 100997, 10 args
		// VR: CalculateActiveNonShadowCasterLights @ 0x141354d20, 11 args (extra char a11 @ RSP+0x58, unused)
		// Verified via Ghidra: VR IsValidLight (ID 98902) @ 0x1412ca950 - same signature as SE
		RE::BSShaderPropertyLightData* lightData = (RE::BSShaderPropertyLightData*)ctx.Rcx;                   // a1
		RE::BSLight**                  lights = (RE::BSLight**)ctx.Rdx;                                       // a2
		int                            maxCount = (int)ctx.R8;                                                // a3
		int*                           shadowLightCount = (int*)ctx.R9;                                       // a4
		RE::ShadowSceneNode*           shadowSceneNode = *((RE::ShadowSceneNode**)(ctx.Rsp + 0x28));          // a5
		RE::BSLightingShaderProperty*  shaderProperty = *((RE::BSLightingShaderProperty**)(ctx.Rsp + 0x30));  // a6
		bool                           addShadowLights = *((bool*)(ctx.Rsp + 0x38));                          // a7
		bool*                          useShadowSun = *((bool**)(ctx.Rsp + 0x40));                            // a8
		bool                           isFirstPerson = *((bool*)(ctx.Rsp + 0x48));                            // a9
		std::uint32_t                  fpMask = *((std::uint32_t*)(ctx.Rsp + 0x50));                          // a10
		RE::BSLight*                   sunLight;
		if (*useShadowSun)
			sunLight = shadowSceneNode->GetRuntimeData().sunShadowDirLight;
		else
			sunLight = shadowSceneNode->GetRuntimeData().sunLight;
		if (shaderProperty->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kCloudLOD))
			sunLight = shadowSceneNode->GetRuntimeData().cloudLight;
		lights[0] = sunLight;
		*shadowLightCount = 0;
		int addedLightCount = 1;
#ifdef ENABLE_SKYRIM_VR
		// VR a11 @ RSP+0x58: if non-zero, skip light accumulation (vanilla behavior)
		if (REL::Module::IsVR() && *(char*)(ctx.Rsp + 0x58) != 0) {
			ctx.Rax = addedLightCount;
			return;
		}
#endif
		// Add shadow caster lights from our extended pool.
		// The original uses lightData->unk1C (accumulated-light bitmask) to skip lights that
		// haven't touched this surface, but we don't populate that mask in CS mode.
		// Fall back to IsValidLight (the same check the original does after the mask).
		if (addShadowLights) {
			auto&        shadowCasters = shadowSceneNode->GetRuntimeData().shadowCasterLights;
			RE::BSLight* prevShadow = nullptr;
			// Step 1: vanilla shadow lights gated by activeLightMask (and first-person mask).
			// Uses the current scene's shadow caster pool, correctly handling menu/special lights.
			for (std::uint32_t slot = 0; slot < shadowCasters.size() && addedLightCount < maxCount; slot++) {
				std::uint32_t bit = 1u << slot;
				if (!((isFirstPerson && (fpMask & bit)) || (lightData->activeLightMask & bit)))
					continue;
				RE::BSLight* sl = (RE::BSLight*)shadowCasters[slot];
				if (!sl || sl == prevShadow || sl == sunLight)
					continue;
				if (BSLightingShaderProperty_IsLightAffectingSurface(shaderProperty, sl)) {
					lights[addedLightCount++] = sl;
					*shadowLightCount = *shadowLightCount + 1;
					prevShadow = sl;
				}
			}
			// Step 2: extended g_lights not already covered by vanilla's activeLightMask slots.
			// Only add lights present in this scene's shadowCasterLights pool
			// (prevents world shadow lights from bleeding into menu/special scenes).
			for (int i = 0; i < settings::iCSDebugLightCount && addedLightCount < maxCount; i++) {
				auto& e = g_lightsAll.Lights[i];
				if (!e.Light || e.Light == sunLight)
					continue;
				bool inScene = false;
				for (std::uint32_t s = 0; s < shadowCasters.size() && !inScene; s++) {
					if ((RE::BSLight*)shadowCasters[s] == e.Light)
						inScene = true;
				}
				if (!inScene)
					continue;
				bool alreadyAdded = false;
				for (int j = 1; j < addedLightCount && !alreadyAdded; j++) {
					if (lights[j] == e.Light)
						alreadyAdded = true;
				}
				if (alreadyAdded)
					continue;
				if (BSLightingShaderProperty_IsLightAffectingSurface(shaderProperty, e.Light)) {
					lights[addedLightCount++] = e.Light;
					*shadowLightCount = *shadowLightCount + 1;
				}
			}
		}
		// Include non-shadow-caster lights from lightData->lights[] (per-surface accumulation list).
		// Use vanilla field checks: skip parabolic shadow-casters (frustrumCull==0xff) and disabled NiLights.
		for (std::uint32_t i = 0; i < lightData->lights.size() && addedLightCount < maxCount; i++) {
			RE::BSLight* l = lightData->lights[i];
			if (!l || l == sunLight)
				continue;
			std::int32_t typeField = *(std::int32_t*)((char*)l + 0x5c);
			std::int64_t niLight = *(std::int64_t*)((char*)l + 0x48);
			if (niLight && (typeField == 0xff || (*(std::uint8_t*)(niLight + 0x10c) & 1)))
				continue;
			lights[addedLightCount++] = l;
		}
		ctx.Rax = addedLightCount;
	}

	static bool HookEx_RenderShadowLights()
	{
		// 1412F9F76
		// 1414CC17D - inlined in AE so need to do it differently
		{
			static _addr addr[] = {
				_addr(100415, REL::Relocate(0xF76 - 0xE30, 0xF76 - 0xE30, 0x1ca), "E8"),
				_addr(107133, 0xC17D - 0xBFF0, "E8"),
			};

			void* a = get_addr(addr);
			if (!a)
				return false;

			if (!HookHelper::WriteHook(a, 5, 0, _HookEx_RenderShadowLights))
				return false;
		}

		return true;
	}

	static bool _SortFunc2(const c_light_entry* first, const c_light_entry* second)
	{
		return first->RedrawScore < second->RedrawScore;
	}

	static void _HookEx_RenderShadowLights(CONTEXT& ctx)
	{
		if (GAME_VER == 1)
			ctx.Rax = 0;

		// VR: the original RenderActiveShadowCasterLights (0x141323190) saves and clears
		// BSGraphics::Renderer::GetDrawStereo() before iterating shadow casters, then restores it.
		// Without this, each hemisphere render is doubled for both eyes → 4-quadrant texture.
		bool vrSavedStereo = false;
		if (REL::Module::IsVR()) {
			vrSavedStereo = RE::BSGraphics::Renderer::GetDrawStereo();
			RE::BSGraphics::Renderer::GetDrawStereo() = false;
		}

#ifdef L_MEASURE_PERFORMANCE
		g_perf.beginLight(nullptr, 1);
#endif

		g_budget.Begin(1);

		std::uint32_t tmp = 0;

		for (int i = 0; i < settings::iCSDebugLightCount; i++) {
			c_light_entry* e = &g_lightsAll.Lights[i];
			if (!e->Light || !e->RedrawFrame)
				continue;

#ifdef PLUGIN_DEBUG_FRAME
			int32_t now = GetCurrentGameFrameCounter();
			if (now > 0 && now == g_debugFrame)
				logs::info("light[{}]->Render({})", i, tmp);
#endif

#ifdef L_MEASURE_PERFORMANCE
			g_perf.beginLight(e->Light, 1);
#endif
			g_budget.BeginLight(e->Light, 1);
			e->Light->Render(tmp);
			g_budget.EndLight(e->Light, 1);
#ifdef L_MEASURE_PERFORMANCE
			g_perf.endLight(e->Light, 1);
#endif
		}

		g_budget.End(1);

#ifdef L_MEASURE_PERFORMANCE
		g_perf.endLight(nullptr, 1);
#endif

		if (REL::Module::IsVR())
			RE::BSGraphics::Renderer::GetDrawStereo() = vrSavedStereo;
	}

	static bool HookEx_AccumulatedLightsArray()
	{
		// Size of accumulated lights array needs to be extended
		if (settings::iCSDebugLightCount > 4) {
			// 1412CFCA4
			static _addr addr[] = {
				_addr(99686, REL::Relocate(0xFCA4 - 0xF950, 0xFCA4 - 0xF950, 0x387), "8D 7D 08 8B D7"),
				_addr(106320, 0xF05 - 0xBB0, "8D 7D 08 85 FF"),
			};

			void* a = get_addr(addr);
			if (!a)
				return false;

			if (!HookHelper::WriteHook(a, 5, 5, _HookEx_AccumulatedLightsArray))
				return false;

			/*unsigned char write = (settings::iCSDebugLightCount + 1) * 2;
			if (!MemoryHelper::WriteByte(MemoryHelper::AddPointer(a, 2), write))
				return false;*/
		}

		return true;
	}

	static void _HookEx_AccumulatedLightsArray(CONTEXT& ctx)
	{
		int needSlots = (settings::iCSDebugLightCount + settings::iCSDebugLightConvertCount + 1) * 2;
		int haveSlots = 10;

		int addSlots = (needSlots - haveSlots);
		if (addSlots > 0) {
			ctx.Rdi += addSlots;
			if (GAME_VER == 0)
				ctx.Rdx += addSlots;
		}
	}

	static bool HookEx_OverwriteShadowMapIndexSelection()
	{
		// Overwrite shadowmap index selection for a light, this is needed because we don't redraw every light every frame so we must choose same buffer as last frame for this light
		// or if not same then at least one that would be "free"

		// SE: RenderCascade+0xBE, index in ESI; VR: RenderCascade+0xE0, index in EDX
		{
			static _addr addr[] = {
				_addr(100820, REL::Relocate(0xA9E - 0x9E0, 0xA9E - 0x9E0, 0xe0), "8B 0D"),  // 0x25
				_addr(107604, 0xDB0 - 0xCF0, "8B 0D"),                                      // 1414F0DB0 to 1414F0DD5 -> 0x25
			};

			void* a = get_addr(addr);
			if (!a)
				return false;

			if (!HookHelper::WriteHook(a, 0x25, 0, _HookEx_OverwriteShadowMapIndexSelection))
				return false;
		}

		return true;
	}

	static int32_t GetBufferIndexForLight(RE::BSShadowLight* light)
	{
		for (int i = 0; i < settings::iCSDebugLightCount; i++) {
			if (g_lightsAll.Lights[i].Light == light)
				return i;
		}

		// This should never happen, and if it does then it's a bug
		return 0;
	}

	static void _HookEx_OverwriteShadowMapIndexSelection(CONTEXT& ctx)
	{
		// SE: this=R15, index out=RSI; VR: this=R14, index out=RDX
		RE::BSShadowLight* light = (RE::BSShadowLight*)REL::Relocate(ctx.R15, ctx.R15, ctx.R14);
		int32_t            idx = GetBufferIndexForLight(light);
		if (REL::Module::IsVR())
			ctx.Rdx = idx;
		else
			ctx.Rsi = idx;

#ifdef PLUGIN_DEBUG_FRAME
		if (IsDebugFrame())
			logs::info("ShadowMapIndexSelect({:X}) = {}", (int64_t)light, idx);
#endif
	}

	static bool HookEx_ExtendDepthBufferArray()
	{
		//int64_t what = (int64_t)&RE::BSGraphics::Renderer::GetSingleton()->GetDepthStencilData().depthStencils[4].views[0];
		//logs::info("DepthBufferPtr: {:X}", what);

		if (settings::iCSDebugLightCount <= 8)
			return true;

		//logs::info("DEBUG: Creating g_normalDepthBuffer");

		int extraCount = settings::iCSDebugLightCount;
		g_normalDepthBuffer = (void**)malloc(sizeof(void*) * extraCount);
		g_readOnlyDepthBuffer = (void**)malloc(sizeof(void*) * extraCount);
		memset(g_normalDepthBuffer, 0, sizeof(void*) * extraCount);
		memset(g_readOnlyDepthBuffer, 0, sizeof(void*) * extraCount);

		// Create more depth buffers for shadow map
		{
			// 1412FD326
			static _addr addr[] = {
				_addr(100458, REL::Relocate(0xD326 - 0xC940, 0xD326 - 0xC940, 0xc91), REL::Relocate("C7 44 24 68 08 00 00 00", "C7 44 24 68 08 00 00 00", "C7 45 A0 08 00 00 00")),
				_addr(107175, 0xBF6 - 0x210, "C7 44 24 68 08 00 00 00"),
			};

			void* a = get_addr(addr);
			if (!a)
				return false;

			if (!MemoryHelper::WriteByte(MemoryHelper::AddPointer(a, REL::Relocate(4, 4, 3)), (uint8_t)settings::iCSDebugLightCount))
				return false;
		}

		// Place the normal extended buffers elsewhere since game struct handles 8 only
		{
			// SE 140D6AB52 / VR 140DBCA00
			void*        a = nullptr;
			static _addr addr[] = {
				_addr(75469, !REL::Module::IsVR() ? 0xB52 - 0x9E0 : 0x1a0, !REL::Module::IsVR() ? "48 8B 01 4D 8D 0C D9" : "4A 8D 04 23 4D 8D 0C C7"),
				_addr(77255, 0x2EB - 0x180, "4C 8D 8F 20 20 00 00"),
			};
			a = get_addr(addr);
			if (!a)
				return false;
			auto sz = REL::Relocate(7, 7, 8);
			if (!HookHelper::WriteHook(a, sz, sz, _HookEx_CreateNormalDepthBuffer))
				return false;
		}

		// Place the read-only extended buffers elsewhere since game struct handles 8 only
		{
			// SE 140D6AB71 / VR 140DBCA24
			static _addr addr[] = {
				_addr(75469, !REL::Module::IsVR() ? 0xB71 - 0x9E0 : 0x1c4, !REL::Module::IsVR() ? "4D 8D 0C D9 4C 8D 45 E7" : "4C 8B 11 4F 8D 0C CF"),
				_addr(77255, 0x2FC - 0x180, "4C 8D 8F 60 20 00 00"),  // 140E452FC
			};

			void* a = get_addr(addr);
			if (!a)
				return false;

			int sz = REL::Relocate(8, 7, 7);
			if (!HookHelper::WriteHook(a, sz, sz, _HookEx_CreateReadOnlyDepthBuffer))
				return false;
		}

		// Set first 8 buffers to game data array
		{
			// SE 140D6AC00 / VR 140DBCAB0
			static _addr addr[] = {
				_addr(75469, !REL::Module::IsVR() ? 0xC00 - 0x9E0 : 0x250, "4C 8D 9C 24 A0 00 00 00"),
				_addr(77255, 0x384 - 0x180, "4C 8D 9C 24 A0 00 00 00"),
			};

			void* a = get_addr(addr);
			if (!a)
				return false;

			if (!HookHelper::WriteHook(a, 8, 8, _HookEx_SetupGameArray))
				return false;
		}

		// Use correct depth buffer when drawing shadows #1
		{
			// 140D70444
			static _addr addr[] = {
				_addr(75580, !REL::Module::IsVR() ? 0x444 - 0x2F0 : 0x1c3, !REL::Module::IsVR() ? "48 6B C8 13 49 03 C9 45 38 78 22 74 0A 49 8B 9C C8 F0 1F 00 00" : "48 6b c8 13 49 03 ca 45 38 60 22 74 0a 49 8b 9c c8 00 22 00 00"),
				_addr(77386, 0x704 - 0x5B0, "45 38 78 22 74 11 48 6B C8 13 49 03 C9 49 8B 9C C8 50 20 00 00"),
			};

			void* a = get_addr(addr);
			if (!a)
				return false;

			if (!HookHelper::WriteHook(a, 21, 0, _HookEx_SelectDepthBuffer1))
				return false;
		}

		// Use correct depth buffer when drawing shadows #2
		{
			// SE 140D6A1A5 / VR 140DBBFFC
			static _addr addr[] = {
				_addr(75462, !REL::Module::IsVR() ? 0x1A5 - 0x070 : 0x19c, !REL::Module::IsVR() ? "74 0A 4C 8B B4 CD 00 20 00 00" : "74 17 48 6B C8 13"),  // SE: +0x135 (JZ + MOV R14; 10 bytes) // VR: +0x19c (JZ after readonly CMP; 6 bytes)
				_addr(77247, 0x985 - 0x850, "74 0A 4C 8B B4 CD 60 20 00 00"),                                                                              // AE: 140E44985
			};

			void* a = get_addr(addr);
			if (!a)
				return false;

			int sz = REL::Relocate(10, 10, 0x2e);  // VR: 0x2e=46 covers JZ+readonly+normal paths to merge point (TEST RBP)
			if (!HookHelper::WriteHook(a, sz, 0, _HookEx_SelectDepthBuffer2))
				return false;
		}

		// Deletion of buffers
		{
			// 140D73E27
			if (GAME_VER == 0) {
				// VR ZeroDepthStencilData (0x140DC6EB0): same pattern "48 83 C3 08 BF 08 00 00 00" at +0x57 as SE ✓
				static _addr addr[] = {
					_addr(75628, 0xE27 - 0xDD0, "48 83 C3 08 BF 08 00 00 00"),
				};

				void* a = get_addr(addr);
				if (!a)
					return false;

				if (!HookHelper::WriteHook(a, 9, -9, _HookEx_DeleteDepthBuffers_SE))
					return false;
			} else if (GAME_VER == 1) {
				// Stupid AE is the worst
				{
					// 140E43195 - Renderer::Shutdown
					static _addr addr[] = {
						_addr(0, 0, ""),
						_addr(77228, 0x3195 - 0x2E10, "49 8D 9E 80 27 00 00"),
					};

					void* a = get_addr(addr);
					if (!a)
						return false;

					if (!HookHelper::WriteHook(a, 7, 7, _HookEx_DeleteDepthBuffers_AE))
						return false;
				}

				// Have to do all this because of bad AE
				{
					// 140E43B8C - unk, maybe static dtor
					static _addr addr[] = {
						_addr(0, 0, ""),
						_addr(77237, 0x3B8C - 0x34A0, "49 8B 4E 70 48 8B 01"),
					};

					void* a = get_addr(addr);
					if (!a)
						return false;

					if (!HookHelper::WriteHook(a, 7, 7, _HookEx_DeleteDepthBuffers_AE))
						return false;
				}

				// So much badness
				{
					// 140E43E79 - not sure, maybe reinit device
					static _addr addr[] = {
						_addr(0, 0, ""),
						_addr(77238, 0x3E79 - 0x3BC0, "FF 90 78 03 00 00"),
					};

					void* a = get_addr(addr);
					if (!a)
						return false;

					if (!HookHelper::WriteHook(a, 6, -6, _HookEx_DeleteDepthBuffers_AE))
						return false;
				}
			}
		}
		return true;
	}

	static void _HookEx_DeleteDepthBuffers_SE(CONTEXT& ctx)
	{
		//logs::info("DEBUG: HookEx_DeleteDepthBuffers");

		RE::BSGraphics::DepthStencilData* data = (RE::BSGraphics::DepthStencilData*)ctx.Rbx;
		if (data == &RE::BSGraphics::Renderer::GetSingleton()->GetDepthStencilData().depthStencils[4]) {
			for (int i = 8; i < settings::iCSDebugLightCount; i++) {
				if (g_normalDepthBuffer[i]) {
					ID3D11DepthStencilView* view = (ID3D11DepthStencilView*)g_normalDepthBuffer[i];
					view->Release();
					g_normalDepthBuffer[i] = nullptr;
				}

				if (g_readOnlyDepthBuffer[i]) {
					ID3D11DepthStencilView* view = (ID3D11DepthStencilView*)g_readOnlyDepthBuffer[i];
					view->Release();
					g_readOnlyDepthBuffer[i] = nullptr;
				}
			}
		}
	}

	static void _HookEx_DeleteDepthBuffers_AE(CONTEXT& ctx)
	{
		for (int i = 8; i < settings::iCSDebugLightCount; i++) {
			if (g_normalDepthBuffer[i]) {
				ID3D11DepthStencilView* view = (ID3D11DepthStencilView*)g_normalDepthBuffer[i];
				view->Release();
				g_normalDepthBuffer[i] = nullptr;
			}

			if (g_readOnlyDepthBuffer[i]) {
				ID3D11DepthStencilView* view = (ID3D11DepthStencilView*)g_readOnlyDepthBuffer[i];
				view->Release();
				g_readOnlyDepthBuffer[i] = nullptr;
			}
		}
	}

	static int32_t GetCurrentDepthTargetType()
	{
		// SE: 14304EEE8; VR: 143180df0
		static REL::RelocationID uid(524780, 388826);
		uintptr_t                addr = uid.address();
		return *((int*)addr);
	}

	static int32_t GetCurrentDepthTargetSubIndex()
	{
		// SE: 14304EEEC; VR: 143180df4
		static REL::RelocationID uid(524780, 388826);  // 524781
		uintptr_t                addr = uid.address();
		addr += 4;  // AE does not have this as a separate ID
		return *((int*)addr);
	}

	static void _HookEx_SelectDepthBuffer1(CONTEXT& ctx)
	{
		RE::BSGraphics::RendererData* data = (RE::BSGraphics::RendererData*)ctx.R8;

		int32_t targetType = GetCurrentDepthTargetType();
		int32_t subIndex = GetCurrentDepthTargetSubIndex();

		if (targetType == 4) {
			/*if (data->readOnlyDepth)
				ctx.Rbx = (DWORD64)RE::BSGraphics::Renderer::GetSingleton()->GetDepthStencilData().depthStencils[targetType].readOnlyViews[subIndex];
			else
				ctx.Rbx = (DWORD64)RE::BSGraphics::Renderer::GetSingleton()->GetDepthStencilData().depthStencils[targetType].views[subIndex];

			DWORD64 prev = ctx.Rbx;*/

			if (data->readOnlyDepth)
				ctx.Rbx = (DWORD64)g_readOnlyDepthBuffer[subIndex];
			else
				ctx.Rbx = (DWORD64)g_normalDepthBuffer[subIndex];

			//logs::info("Writing1 readonly: {} depth buffer in RBX {:X} -> {:X} [{}, {}]", data->readOnlyDepth ? 1 : 0, (int64_t)prev, (int64_t)ctx.Rbx, targetType, subIndex);
		} else {
			if (data->readOnlyDepth)
				ctx.Rbx = (DWORD64)RE::BSGraphics::Renderer::GetSingleton()->GetDepthStencilData().depthStencils[targetType].readOnlyViews[subIndex];
			else
				ctx.Rbx = (DWORD64)RE::BSGraphics::Renderer::GetSingleton()->GetDepthStencilData().depthStencils[targetType].views[subIndex];
		}
	}

	static void _HookEx_SelectDepthBuffer2(CONTEXT& ctx)
	{
		// VR: renderer=R14, readonly via GetRuntimeData(), result in RBP
		// SE/AE: renderer=RBP, readonly via GetRuntimeData(), result in R14
		bool isVR = REL::Module::IsVR();
		bool isReadOnly;
		if (isVR)
			isReadOnly = ((RE::BSGraphics::Renderer*)ctx.R14)->GetRuntimeData().readOnlyDepth;
		else
			isReadOnly = ((RE::BSGraphics::Renderer*)ctx.Rbp)->GetRuntimeData().readOnlyDepth;

		int32_t targetType = GetCurrentDepthTargetType();
		int32_t subIndex = GetCurrentDepthTargetSubIndex();

		DWORD64 result;
		if (targetType == 4) {
			/*if (isReadOnly)
				result = (DWORD64)RE::BSGraphics::Renderer::GetSingleton()->GetDepthStencilData().depthStencils[targetType].readOnlyViews[subIndex];
			else
				result = (DWORD64)RE::BSGraphics::Renderer::GetSingleton()->GetDepthStencilData().depthStencils[targetType].views[subIndex];

			if (isReadOnly)*/
			if (isReadOnly)
				result = (DWORD64)g_readOnlyDepthBuffer[subIndex];
			else
				result = (DWORD64)g_normalDepthBuffer[subIndex];
		} else {
			if (isReadOnly)
				result = (DWORD64)RE::BSGraphics::Renderer::GetSingleton()->GetDepthStencilData().depthStencils[targetType].readOnlyViews[subIndex];
			else
				result = (DWORD64)RE::BSGraphics::Renderer::GetSingleton()->GetDepthStencilData().depthStencils[targetType].views[subIndex];
		}

		if (isVR)
			ctx.Rbp = result;
		else
			ctx.R14 = result;
	}

	static void _HookEx_SetupGameArray(CONTEXT& ctx)
	{
		// VR: R13 = a_target * 0x13; SE/AE: R12 = a_target * 0x13
		if (REL::Relocate(ctx.R12, ctx.R12, ctx.R13) != 4 * 19)
			return;

		//logs::info("DEBUG: HookEx_SetupGameArray");

		RE::BSGraphics::Renderer* render = (RE::BSGraphics::Renderer*)ctx.R15;
		for (int i = 0; i < 8; i++) {
			//logs::info("DEBUG: g_normalDepthBuffer[{}] = {:X}", i, (int64_t)g_normalDepthBuffer[i]);

			render->GetDepthStencilData().depthStencils[4].views[i] = (ID3D11DepthStencilView*)g_normalDepthBuffer[i];
			render->GetDepthStencilData().depthStencils[4].readOnlyViews[i] = (ID3D11DepthStencilView*)g_readOnlyDepthBuffer[i];
		}
	}

	static void _HookEx_CreateNormalDepthBuffer(CONTEXT& ctx)
	{
		// VR: R13 = a_target * 0x13, loop index in RBX; SE: R12 = a_target * 0x13, loop index in RDI
		if (REL::Relocate(ctx.R12, ctx.R12, ctx.R13) != 4 * 19)
			return;

		int index = (int)REL::Relocate(ctx.Rdi, ctx.Rbx, ctx.Rbx);  // SE: RDI; AE/VR: RBX

		//logs::info("DEBUG: Creating g_normalDepthBuffer[{}]", index);

		ctx.R9 = (DWORD64)&g_normalDepthBuffer[index];
	}

	static void _HookEx_CreateReadOnlyDepthBuffer(CONTEXT& ctx)
	{
		// VR: R13 = a_target * 0x13, loop index in RBX; SE: R12 = a_target * 0x13, loop index in RDI
		if (REL::Relocate(ctx.R12, ctx.R12, ctx.R13) != 4 * 19)
			return;

		int index = (int)REL::Relocate(ctx.Rdi, ctx.Rbx, ctx.Rbx);  // SE: RDI; AE/VR: RBX

		ctx.R9 = (DWORD64)&g_readOnlyDepthBuffer[index];
	}

	static bool HookEx_UseMoreDepthBuffers()
	{
		// Allow using more than 4 depth buffers for lights
		/*if (settings::iCSDebugLightCount > 4) {
			// SE 141E10538 / VR 141ED62F0; depth buffer count mask used identically in VR
			static _addr addr[] = {
				_addr(513748, 0, "0F 00 00 00"),
				_addr(391715, 0, "0F 00 00 00"),
			};

			void* a = get_addr(addr);
			if (!a)
				return false;

			uint64_t newMask = (uint64_t)1;
			newMask <<= settings::iCSDebugLightCount;
			newMask--;
			if (!MemoryHelper::WriteUInt32(a, (uint32_t)newMask))
				return false;
		}*/

		return true;
	}

	static bool HookEx_DisableFocusShadows()
	{
		// Needed for Community Shaders because we don't use them there anyway and they would overwrite the extra depth buffers we used
		// IDs 10209/10207: focus-shadow thunks; SE/AE/VR all use 6-byte MOV EAX — same pattern, offset 0
		uint8_t newBytes[6]{ 0x48, 0x31, 0xC0, 0x90, 0x90, 0x90 };

		{
			// SE 1400EB2F0 / VR 1400FC740 - 10209
			static _addr addr[] = {
				_addr(10209, 0, "8B 05"),
				_addr(10247, 0, "8B 05"),
			};

			void* a = get_addr(addr);
			if (!a || !MemoryHelper::WriteBytes(a, newBytes, 6))
				return false;
		}

		// Second place
		{
			// SE 1400EB2D0 / VR 1400FC720 - 10207
			static _addr addr[] = {
				_addr(10207, 0, "8B 05"),
				_addr(10245, 0, "8B 05"),
			};

			void* a = get_addr(addr);
			if (!a || !MemoryHelper::WriteBytes(a, newBytes, 6))
				return false;
		}

		// This byte needs to be set to 0
		{
			// 141E33EB3
			static _addr addr[] = {
				_addr(513201, 0, "01"),
				_addr(390932, 0, "01"),
			};

			void* a = get_addr(addr);
			if (!a || !MemoryHelper::WriteByte(a, 0))
				return false;
		}

		return true;
	}

	static bool HookEx_DisableColorMaskDrawing()
	{
		// 1412FAF20
		static _addr addr[] = {
			_addr(100422, REL::Relocate(0xF20 - 0xE90, 0xF20 - 0xE90, 0x9e), "E8"),
			_addr(107140, 0x67E - 0x600, "E8"),
		};

		void* a = get_addr(addr);
		void* cave = HookHelper::FindCodeCave(a);
		if (!cave)
			return false;

		HookHelper::WriteAbsoluteJump(cave, reinterpret_cast<void*>(&_HookEx_DisableColorMaskDrawing));
		HookHelper::WriteRelCall(a, cave);

		return true;
	}

	static void _HookEx_DisableColorMaskDrawing()
	{
		auto shadowSceneNode = GetShadowSceneNode();
		for (int i = 0;;) {
			auto l = shadowSceneNode->GetRuntimeData().shadowCasterLights[i];
			if (!l)
				break;

			l->ReturnShadowmaps();  // ClearShadowMapData

			i += l->shadowMapCount;
		}
	}

	static bool HookEx_FixMaskOverflow()
	{
		// When it creates color map, it will try to get target depth buffer index from out of bounds
		{
			// 1412FAFCC
			static _addr addr[] = {
				_addr(100423, !REL::Module::IsVR() ? 0xFCC - 0xF50 : 0xe4, !REL::Module::IsVR() ? "4C 8D 3D" : "4C 8D 35"),
				_addr(107141, 0x72B - 0x6A0, "48 8D 2D"),
			};

			void* a = get_addr(addr);
			if (!a || !HookHelper::WriteHook(a, 7, 0, _Hook_FixMaskOverflow))
				return false;
		}

		return true;
	}

	static void _Hook_FixMaskOverflow(CONTEXT& ctx)
	{
		static int* _tmp_arr = nullptr;
		if (!_tmp_arr) {
			int mlightCount = 4;
			if (settings::iCSDebugLightCount > 4)
				mlightCount = settings::iCSDebugLightCount;

			_tmp_arr = (int*)malloc(sizeof(int) * mlightCount);
			for (int i = 0; i < mlightCount; i++)
				_tmp_arr[i] = 5 + (i % 4);
		}

		if (GAME_VER == 0)
			if (!REL::Module::IsVR())
				ctx.R15 = (DWORD64)&_tmp_arr[0];
			else
				ctx.R14 = (DWORD64)&_tmp_arr[0];
		else if (GAME_VER == 1)
			ctx.Rbp = (DWORD64)&_tmp_arr[0];
	}

	static bool HookEx_CalculateActiveLights()
	{
		static _addr addr[] = {
			_addr(100419, 0, REL::Relocate("48 89 6C 24 18", "48 89 6C 24 18", "48 89 6C 24 10")),
			_addr(107137, 0, "48 89 6C 24 18"),
		};

		void* a = get_addr(addr);
		void* cave = HookHelper::FindCodeCave(a);
		if (!cave)
			return false;

		HookHelper::WriteAbsoluteJump(cave, reinterpret_cast<void*>(&CalculateActiveShadowCasterLights_CS));
		HookHelper::WriteRelJump(a, cave);

		// Called when a light accumulates a surface
		/*{
			// 1412E15A7
			static _addr addr[] = {
				_addr(99923, 0x5A7 - 0x4E0, "81 FA FF FF 00 00"), // 5BC - 5A7 = 15
				_addr(106567, 0x20A - 0x140, "41 81 F8 FF FF 00 00"), // 220 - 20a = 16
			};

			void* a = get_addr(addr);
			if (!a)
				return false;

			int sz = GAME_VER == 0 ? 0x15 : 0x16;
			if (!HookHelper::WriteHook(a, sz, 0, _HookEx_OnAccumulatingLight))
				return false;
		}*/

		// Ctor for BSShaderPropertyLightData, need to set our extra field to 0
		/*{
			// 1412DC5EA
			static _addr addr[] = {
				_addr(99854, 0x5EA - 0x4C0, "89 77 1C 40 88 77 20"),
				_addr(106499, 0xD4A - 0xC20, "89 77 1C 40 88 77 20"), // 1414ACD4A
			};

			void* a = get_addr(addr);
			if (!a)
				return false;

			if (!HookHelper::WriteHook(a, 7, 7, _HookEx_BSLightingShaderProperty_ctor))
				return false;
		}*/

		// Calculate which lights to pick for surface
		{
			// 1413281E0
			static _addr addr[] = {
				_addr(100997, 0, "4C 89 4C 24 20"),
				_addr(107784, 0, "4C 89 4C 24 20"),
			};

			void* a = get_addr(addr);
			if (!a)
				return false;

			if (!HookHelper::WriteHook(a, 5, 0, _HookEx_CalculateActiveLightsForSurface))
				return false;

			// ret
			if (!MemoryHelper::WriteByte(MemoryHelper::AddPointer(a, 5), 0xC3))
				return false;
		}

		return true;
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
