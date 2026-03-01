#pragma once

#ifdef _WIN64
typedef int64_t _i_pointer;
#else
typedef int32_t _i_pointer;
#endif

struct MemoryHelper
{
	MemoryHelper() = delete;

	/// <summary>
	/// Modify pointer and return it.
	/// </summary>
	/// <param name="address">The address.</param>
	/// <param name="offset">The offset to add or subtract from the address.</param>
	/// <returns></returns>
	static void* AddPointer(void* address, int32_t offset)
	{
		_i_pointer v = (_i_pointer)address;
		v += offset;
		return (void*)v;
	}

	/// <summary>
	/// Check if a specific address can be read from right now.
	/// </summary>
	/// <param name="address">The address we wish to read from.</param>
	/// <param name="length">The length of bytes we wish to read there.</param>
	/// <returns></returns>
	static bool CanRead(void* address, int32_t length)
	{
		MEMORY_BASIC_INFORMATION info;
		if (VirtualQuery(address, &info, sizeof(MEMORY_BASIC_INFORMATION)) == 0) {
			ReportWinapiError();
			return false;
		}

		_i_pointer begin = (_i_pointer)info.BaseAddress;
		_i_pointer end = begin + (_i_pointer)info.RegionSize;

		_i_pointer a = (_i_pointer)address;
		_i_pointer b = a + length;

		if (a < begin || a > end || b < begin || b > end)
			return false;

		if (info.State != MEM_COMMIT)
			return false;

		if (info.Protect == 0)
			return false;

		switch (info.Protect) {
		case PAGE_EXECUTE_READWRITE:
		case PAGE_READWRITE:
		case PAGE_EXECUTE_READ:
		case PAGE_READONLY:
			break;

		default:
			return false;
		}

		return true;
	}

	/// <summary>
	/// Get segment's begin and end address.
	/// </summary>
	/// <param name="address">The address where segment is.</param>
	/// <param name="begin">The result begin address if returned true.</param>
	/// <param name="end">The result end address if returned true.</param>
	/// <returns></returns>
	static bool GetSegmentRange(void* address, void*& begin, void*& end)
	{
		MEMORY_BASIC_INFORMATION info;
		if (VirtualQuery(address, &info, sizeof(MEMORY_BASIC_INFORMATION)) == 0) {
			ReportWinapiError();
			return false;
		}

		begin = (void*)info.BaseAddress;
		end = AddPointer(begin, (int32_t)info.RegionSize);
		return true;
	}

	/// <summary>
	/// Test if bytes at specified address match.
	/// </summary>
	/// <param name="address">The address to check.</param>
	/// <param name="hex">Hex string delimited by space. Use ?? for wildcard byte.</param>
	/// <returns></returns>
	static bool TestBytes(void* address, std::string_view hex)
	{
		std::vector<uint16_t> vec;

		if (!ParseHex(hex, vec)) {
			CriticalFail(Format("Failed to parse bytes from hex string '{}'!", hex.data()));
			return false;
		}

		if (vec.empty()) {
			if (!CanRead(address, 1))
				return false;

			return true;
		}

		int32_t len = (int32_t)vec.size();

		uint8_t* buf = (uint8_t*)malloc(len);
		if (!buf)
			return false;

		if (!SafeRead(address, buf, len)) {
			free(buf);
			return false;
		}

		for (int32_t i = 0; i < len; i++) {
			uint16_t a = vec.at(i);
			if (a == 0xFFFF)
				continue;

			uint8_t b = buf[i];

			if (a != b) {
				free(buf);
				return false;
			}
		}

		free(buf);
		return true;
	}

	/// <summary>
	/// Ensures the bytes at address match, or does critical fail if doesn't match.
	/// </summary>
	/// <param name="address">The address to check.</param>
	/// <param name="hex">Hex string delimited by space. Use ?? for wildcard byte.</param>
	/// <param name="what">Name of location for error message.</param>
	static void EnsureBytes(void* address, std::string_view hex, std::string_view what)
	{
		if (TestBytes(address, hex))
			return;

		std::string message = Format("Failed to match bytes at {}:{}!", address, what.empty() ? "UNKNOWN" : what.data());
		CriticalFail(message);
	}

	/// <summary>
	/// Ensures the bytes at address match, or does critical fail if doesn't match.
	/// </summary>
	/// <param name="address">The address to check.</param>
	/// <param name="hex">Hex string delimited by space. Use ?? for wildcard byte.</param>
	static void EnsureBytes(void* address, std::string_view hex)
	{
		EnsureBytes(address, hex, "");
	}

	/// <summary>
	/// Report an error to user and exit app.
	/// </summary>
	/// <param name="message">The error message.</param>
	static void CriticalFail(std::string_view message)
	{
		// TODO: message box and somehow kill app?
		throw std::exception(message.data());
	}

	/// <summary>
	/// Formats the specified string.
	/// </summary>
	/// <typeparam name="Args"></typeparam>
	/// <param name="str"></param>
	/// <param name="args"></param>
	/// <returns></returns>
	template <typename... Args>
	static std::string Format(std::string_view str, Args&&... args)
	{
		auto store = std::make_tuple(std::forward<Args>(args)...);
		return std::apply([&](auto&... a) {
			return std::vformat(str, std::make_format_args(a...));
		},
			store);
	}

	/// <summary>
	/// Convert bytes to hex string.
	/// </summary>
	/// <param name="buf">The buffer of bytes.</param>
	/// <param name="size">The length of buffer.</param>
	/// <returns></returns>
	static std::string ToHex(const uint8_t* buf, int32_t size)
	{
		std::stringstream ss;
		ss << std::setfill('0') << std::hex;

		for (int i = 0; i < size; i++) {
			if (i > 0)
				ss << ' ';
			ss << std::setw(2);
			ss << (int)buf[i];
		}

		return ss.str();
	}

	/// <summary>
	/// Parse input into bytes. Valid input examples:<para></para>
	/// "af 00 ?? c0"<para></para>
	/// "AF-00-**-C0"<para></para>
	/// "af00??C0"<para></para>
	/// Wildcard is written as 0xFFFF in result.
	/// </summary>
	/// <param name="input">The input string.</param>
	/// <param name="result">The result.</param>
	/// <returns></returns>
	static bool ParseHex(std::string_view input, std::vector<uint16_t>& result)
	{
		int32_t index = 0;
		int32_t len = (int32_t)input.length();

		if (len == 0)
			return true;

		uint8_t state = 1;
		uint8_t data = 0;

		while (index < len) {
			char ch = input[index];

			// Specifically allow no delimiter.
			if (state == 0 && ((ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F') || (ch >= '0' && ch <= '9') || ch == '?' || ch == '*'))
				state = 1;

			if (state == 0) {
				if (ch != ' ' && ch != '-')
					return false;

				state = 1;
				index++;
				continue;
			}

			if (state == 1) {
				if (ch == '*' || ch == '?') {
					state = 3;
					index++;
					continue;
				}

				if (ch >= '0' && ch <= '9') {
					data = (uint8_t)(ch - '0');
					data <<= 4;
					state = 2;
					index++;
					continue;
				}

				if (ch >= 'a' && ch <= 'f') {
					data = (uint8_t)(ch - 'a');
					data += 10;
					data <<= 4;
					state = 2;
					index++;
					continue;
				}

				if (ch >= 'A' && ch <= 'F') {
					data = (uint8_t)(ch - 'A');
					data += 10;
					data <<= 4;
					state = 2;
					index++;
					continue;
				}

				return false;
			}

			if (state == 2) {
				if (ch >= '0' && ch <= '9') {
					data |= (uint8_t)(ch - '0');
					result.push_back(data);
					data = 0;
					state = 0;
					index++;
					continue;
				}

				if (ch >= 'a' && ch <= 'f') {
					data |= (uint8_t)(ch - 'a');
					data += 10;
					result.push_back(data);
					data = 0;
					state = 0;
					index++;
					continue;
				}

				if (ch >= 'A' && ch <= 'F') {
					data |= (uint8_t)(ch - 'A');
					data += 10;
					result.push_back(data);
					data = 0;
					state = 0;
					index++;
					continue;
				}

				return false;
			}

			if (state == 3) {
				if (ch == '*' || ch == '?') {
					result.push_back(0xFFFF);
					state = 0;
					index++;
					continue;
				}

				return false;
			}

			throw std::exception("Invalid state in MemoryHelper::ParseHex!");
		}

		if (state != 0)
			return false;

		return true;
	}

	/// <summary>
	/// Read bytes from specified address.
	/// </summary>
	/// <param name="address">The address to read from.</param>
	/// <param name="buf">The buffer to read into.</param>
	/// <param name="length">The length of bytes to read.</param>
	/// <returns></returns>
	static bool ReadBytes(void* address, uint8_t* buf, int32_t length)
	{
		return SafeRead(address, buf, length);
	}

	/// <summary>
	/// Write bytes to specified address.
	/// </summary>
	/// <param name="address">The address to write to.</param>
	/// <param name="buf">The buffer for bytes to write.</param>
	/// <param name="length">The length of bytes to write.</param>
	/// <returns></returns>
	static bool WriteBytes(void* address, const uint8_t* buf, int32_t length)
	{
		return SafeWrite(address, buf, length);
	}

	/// <summary>
	/// Read byte from specified address.
	/// </summary>
	/// <param name="address">The address to read from.</param>
	/// <param name="value">The value to read into.</param>
	/// <returns></returns>
	static bool ReadByte(void* address, uint8_t& value)
	{
		return SafeRead(address, &value, sizeof(uint8_t));
	}

	/// <summary>
	/// Write byte to specified address.
	/// </summary>
	/// <param name="address">The address to write to.</param>
	/// <param name="value">The value to write.</param>
	/// <returns></returns>
	static bool WriteByte(void* address, uint8_t value)
	{
		return SafeWrite(address, &value, sizeof(uint8_t));
	}

	/// <summary>
	/// Read signed byte from specified address.
	/// </summary>
	/// <param name="address">The address to read from.</param>
	/// <param name="value">The value to read into.</param>
	/// <returns></returns>
	static bool ReadSByte(void* address, int8_t& value)
	{
		uint8_t buf = 0;
		if (!SafeRead(address, &buf, sizeof(int8_t)))
			return false;
		value = (int8_t)buf;
		return true;
	}

	/// <summary>
	/// Write signed byte to specified address.
	/// </summary>
	/// <param name="address">The address to write to.</param>
	/// <param name="value">The value to write.</param>
	/// <returns></returns>
	static bool WriteSByte(void* address, int8_t value)
	{
		return SafeWrite(address, (uint8_t*)&value, sizeof(int8_t));
	}

	/// <summary>
	/// Read unsigned short from specified address.
	/// </summary>
	/// <param name="address">The address to read from.</param>
	/// <param name="value">The value to read into.</param>
	/// <returns></returns>
	static bool ReadUInt16(void* address, uint16_t& value)
	{
		uint8_t buf[2];
		if (!SafeRead(address, buf, sizeof(uint16_t)))
			return false;
		value = *((uint16_t*)buf);
		return true;
	}

	/// <summary>
	/// Write unsigned short to specified address.
	/// </summary>
	/// <param name="address">The address to write to.</param>
	/// <param name="value">The value to write.</param>
	/// <returns></returns>
	static bool WriteUInt16(void* address, uint16_t value)
	{
		return SafeWrite(address, (uint8_t*)&value, sizeof(uint16_t));
	}

	/// <summary>
	/// Read signed short from specified address.
	/// </summary>
	/// <param name="address">The address to read from.</param>
	/// <param name="value">The value to read into.</param>
	/// <returns></returns>
	static bool ReadInt16(void* address, int16_t& value)
	{
		uint8_t buf[2];
		if (!SafeRead(address, buf, sizeof(int16_t)))
			return false;
		value = *((int16_t*)buf);
		return true;
	}

	/// <summary>
	/// Write signed short to specified address.
	/// </summary>
	/// <param name="address">The address to write to.</param>
	/// <param name="value">The value to write.</param>
	/// <returns></returns>
	static bool WriteInt16(void* address, int16_t value)
	{
		return SafeWrite(address, (uint8_t*)&value, sizeof(int16_t));
	}

	/// <summary>
	/// Read unsigned integer from specified address.
	/// </summary>
	/// <param name="address">The address to read from.</param>
	/// <param name="value">The value to read into.</param>
	/// <returns></returns>
	static bool ReadUInt32(void* address, uint32_t& value)
	{
		uint8_t buf[4];
		if (!SafeRead(address, buf, sizeof(uint32_t)))
			return false;
		value = *((uint32_t*)buf);
		return true;
	}

	/// <summary>
	/// Write unsigned integer to specified address.
	/// </summary>
	/// <param name="address">The address to write to.</param>
	/// <param name="value">The value to write.</param>
	/// <returns></returns>
	static bool WriteUInt32(void* address, uint32_t value)
	{
		return SafeWrite(address, (uint8_t*)&value, sizeof(uint32_t));
	}

	/// <summary>
	/// Read signed integer from specified address.
	/// </summary>
	/// <param name="address">The address to read from.</param>
	/// <param name="value">The value to read into.</param>
	/// <returns></returns>
	static bool ReadInt32(void* address, int32_t& value)
	{
		uint8_t buf[4];
		if (!SafeRead(address, buf, sizeof(int32_t)))
			return false;
		value = *((int32_t*)buf);
		return true;
	}

	/// <summary>
	/// Write signed integer to specified address.
	/// </summary>
	/// <param name="address">The address to write to.</param>
	/// <param name="value">The value to write.</param>
	/// <returns></returns>
	static bool WriteInt32(void* address, int32_t value)
	{
		return SafeWrite(address, (uint8_t*)&value, sizeof(int32_t));
	}

	/// <summary>
	/// Read unsigned long from specified address.
	/// </summary>
	/// <param name="address">The address to read from.</param>
	/// <param name="value">The value to read into.</param>
	/// <returns></returns>
	static bool ReadUInt64(void* address, uint64_t& value)
	{
		uint8_t buf[8];
		if (!SafeRead(address, buf, sizeof(uint64_t)))
			return false;
		value = *((uint64_t*)buf);
		return true;
	}

	/// <summary>
	/// Write unsigned long to specified address.
	/// </summary>
	/// <param name="address">The address to write to.</param>
	/// <param name="value">The value to write.</param>
	/// <returns></returns>
	static bool WriteUInt64(void* address, uint64_t value)
	{
		return SafeWrite(address, (uint8_t*)&value, sizeof(uint64_t));
	}

	/// <summary>
	/// Read signed long from specified address.
	/// </summary>
	/// <param name="address">The address to read from.</param>
	/// <param name="value">The value to read into.</param>
	/// <returns></returns>
	static bool ReadInt64(void* address, int64_t& value)
	{
		uint8_t buf[8];
		if (!SafeRead(address, buf, sizeof(int64_t)))
			return false;
		value = *((int64_t*)buf);
		return true;
	}

	/// <summary>
	/// Write signed long to specified address.
	/// </summary>
	/// <param name="address">The address to write to.</param>
	/// <param name="value">The value to write.</param>
	/// <returns></returns>
	static bool WriteInt64(void* address, int64_t value)
	{
		return SafeWrite(address, (uint8_t*)&value, sizeof(int64_t));
	}

	/// <summary>
	/// Read float from specified address.
	/// </summary>
	/// <param name="address">The address to read from.</param>
	/// <param name="value">The value to read into.</param>
	/// <returns></returns>
	static bool ReadFloat(void* address, float& value)
	{
		uint8_t buf[4];
		if (!SafeRead(address, buf, sizeof(float)))
			return false;
		value = *((float*)buf);
		return true;
	}

	/// <summary>
	/// Write float to specified address.
	/// </summary>
	/// <param name="address">The address to write to.</param>
	/// <param name="value">The value to write.</param>
	/// <returns></returns>
	static bool WriteFloat(void* address, float value)
	{
		return SafeWrite(address, (uint8_t*)&value, sizeof(float));
	}

	/// <summary>
	/// Read double from specified address.
	/// </summary>
	/// <param name="address">The address to read from.</param>
	/// <param name="value">The value to read into.</param>
	/// <returns></returns>
	static bool ReadDouble(void* address, double& value)
	{
		uint8_t buf[8];
		if (!SafeRead(address, buf, sizeof(double)))
			return false;
		value = *((double*)buf);
		return true;
	}

	/// <summary>
	/// Write double to specified address.
	/// </summary>
	/// <param name="address">The address to write to.</param>
	/// <param name="value">The value to write.</param>
	/// <returns></returns>
	static bool WriteDouble(void* address, double value)
	{
		return SafeWrite(address, (uint8_t*)&value, sizeof(double));
	}

	/// <summary>
	/// Read null terminated string from specified address.
	/// </summary>
	/// <param name="address">The address to read from.</param>
	/// <param name="value">The value to read into.</param>
	/// <param name="maxLength">The maximum length to read before not finding terminating null and giving up.</param>
	/// <returns></returns>
	static bool ReadString(void* address, std::string& value, int32_t maxLength = 1024)
	{
		char*   strBuf = nullptr;
		int32_t strLen = 0;

		const int32_t STRING_BUF_SIZE = 128;
		uint8_t       buf[STRING_BUF_SIZE];
		bool          ok = false;
		while (true) {
			if (!SafeRead(address, buf, STRING_BUF_SIZE))
				break;

			if (buf[0] == 0) {
				ok = true;
				break;
			}

			if (strBuf == nullptr)
				strBuf = (char*)malloc(STRING_BUF_SIZE);
			else
				strBuf = (char*)realloc(strBuf, strLen + STRING_BUF_SIZE);

			for (int32_t i = 0; i < STRING_BUF_SIZE; i++) {
				if (buf[i] == 0) {
					strBuf[strLen + i] = '\0';
					strLen += i;
					ok = true;
					break;
				}

				strBuf[strLen + i] = (char)buf[i];
			}

			if (ok)
				break;

			strLen += STRING_BUF_SIZE;

			if (strLen >= maxLength)
				break;
		}

		if (!ok) {
			if (strBuf)
				free(strBuf);

			return false;
		}

		if (strBuf) {
			value = std::string(strBuf, strLen);
			free(strBuf);
		} else
			value = std::string();

		return true;
	}

	/// <summary>
	/// Read pointer from specified address.
	/// </summary>
	/// <param name="address">The address to read from.</param>
	/// <param name="value">The value to read into.</param>
	/// <returns></returns>
	static bool ReadPointer(void* address, void*& value)
	{
		uint8_t buf[sizeof(void*)];
		if (!SafeRead(address, buf, sizeof(void*)))
			return false;
		value = *((void**)buf);
		return true;
	}

	/// <summary>
	/// Write pointer to specified address.
	/// </summary>
	/// <param name="address">The address to write to.</param>
	/// <param name="value">The value to write.</param>
	/// <returns></returns>
	static bool WritePointer(void* address, void* value)
	{
		return SafeWrite(address, (uint8_t*)&value, sizeof(void*));
	}

private:
	static void ReportWinapiError()
	{
		//DWORD error = GetLastError();
		// ... TODO?
	}

	static void _read(void* address, uint8_t* buf, int32_t length)
	{
		memcpy(buf, address, length);
	}

	static void _write(void* address, const uint8_t* buf, int32_t length)
	{
		memcpy(address, buf, length);
	}

	static bool SafeRead(void* address, uint8_t* buf, int32_t length)
	{
		if (!CanRead(address, length))
			return false;

		_read(address, buf, length);
		return true;
	}

	static bool SafeWrite(void* address, const uint8_t* buf, int32_t length)
	{
		MEMORY_BASIC_INFORMATION info;
		if (VirtualQuery(address, &info, sizeof(MEMORY_BASIC_INFORMATION)) == 0) {
			ReportWinapiError();
			return false;
		}

		_i_pointer begin = (_i_pointer)info.BaseAddress;
		_i_pointer end = begin + (_i_pointer)info.RegionSize;

		_i_pointer a = (_i_pointer)address;
		_i_pointer b = a + length;

		if (a < begin || a > end || b < begin || b > end)
			return false;

		if (info.State != MEM_COMMIT)
			return false;

		if (info.Protect == 0)
			return false;

		switch (info.Protect) {
		case PAGE_EXECUTE_READWRITE:
		case PAGE_READWRITE:
			_write(address, buf, length);
			return true;
		}

		DWORD flProtect = 0;
		switch (info.Protect) {
		case PAGE_EXECUTE:
		case PAGE_EXECUTE_READ:
			if (!VirtualProtect(address, length, PAGE_EXECUTE_READWRITE, &flProtect)) {
				ReportWinapiError();
				return false;
			}
			break;

		case PAGE_READONLY:
			if (!VirtualProtect(address, length, PAGE_READWRITE, &flProtect)) {
				ReportWinapiError();
				return false;
			}
			break;

		default:
			return false;
		}

		_write(address, buf, length);

		DWORD tempProtect = 0;
		VirtualProtect(address, length, flProtect, &tempProtect);

		return true;
	}
};

/// <summary>
/// Helper for searching something in memory.
/// </summary>
struct MemoryScanner
{
	MemoryScanner()
	{
		BeginAddress = 0;
		EndAddress = (void*)0x7FFFFFFFFFFFFFFF;
		Alignment = 1;
		IsExecutable = 0;
		IsWritable = 0;
		MaxResults = 1000;
	}

	/// <summary>
	/// The starting address where we should search.
	/// </summary>
	void* BeginAddress;

	/// <summary>
	/// The end address where we should stop search.
	/// </summary>
	void* EndAddress;

	/// <summary>
	/// The alignment of what we are searching. Setting this will greatly speed up the search.
	/// </summary>
	int Alignment;

	/// <summary>
	/// If positive then the result must be in executable segment. If negative then must not be in that segment.
	/// </summary>
	int IsExecutable;

	/// <summary>
	/// If positive then the result must be in writable segment. If negative then must not be in that segment.
	/// </summary>
	int IsWritable;

	/// <summary>
	/// Count of max results.
	/// </summary>
	int MaxResults;

	/// <summary>
	/// This is what we will be searching for. 0xFFFF means wildcard, anything else means that byte value.
	/// </summary>
	std::vector<uint16_t> Input;

	/// <summary>
	/// The result will be here after scanning.
	/// </summary>
	std::vector<void*> Result;

	/// <summary>
	/// Perform the scan now. This does not clear the previous result if re-scanning!
	/// </summary>
	void Scan()
	{
		if (Input.empty())
			return;

		SYSTEM_INFO si;
		GetSystemInfo(&si);

		_i_pointer minAddr = (_i_pointer)si.lpMinimumApplicationAddress;
		_i_pointer maxAddr = (_i_pointer)si.lpMaximumApplicationAddress;

		{
			_i_pointer minSearch = (_i_pointer)BeginAddress;
			_i_pointer maxSearch = (_i_pointer)EndAddress;

			if (minAddr < minSearch)
				minAddr = minSearch;
			if (maxAddr > maxSearch)
				maxAddr = maxSearch;
		}

		_i_pointer cur = minAddr;
		while (cur < maxAddr) {
			MEMORY_BASIC_INFORMATION mbi;
			if (VirtualQuery((void*)cur, &mbi, sizeof(MEMORY_BASIC_INFORMATION)) == 0)
				break;

			_i_pointer minRegion = (_i_pointer)mbi.BaseAddress;
			_i_pointer maxRegion = minRegion + (_i_pointer)mbi.RegionSize;

			if (cur == maxRegion)
				break;
			cur = maxRegion;

			if (mbi.State != MEM_COMMIT)
				continue;

			switch (mbi.Protect) {
			case PAGE_EXECUTE_READWRITE:
			case PAGE_READWRITE:
			case PAGE_EXECUTE_READ:
			case PAGE_READONLY:
				break;

			default:
				continue;
			}

			if (IsWritable > 0) {
				if (mbi.Protect != PAGE_EXECUTE_READWRITE && mbi.Protect != PAGE_READWRITE)
					continue;
			} else if (IsWritable < 0) {
				if (mbi.Protect == PAGE_EXECUTE_READWRITE || mbi.Protect == PAGE_READWRITE)
					continue;
			}

			if (IsExecutable > 0) {
				if (mbi.Protect != PAGE_EXECUTE_READ && mbi.Protect != PAGE_EXECUTE_READWRITE)
					continue;
			} else if (IsExecutable < 0) {
				if (mbi.Protect == PAGE_EXECUTE_READ || mbi.Protect == PAGE_EXECUTE_READWRITE)
					continue;
			}

			_i_pointer begin = minRegion;
			if (begin < minAddr)
				begin = minAddr;

			_i_pointer end = maxRegion;
			if (end > maxAddr)
				end = maxAddr;

			if (begin < end)
				ScanRegion(begin, end);
		}
	}

private:
	void ScanRegion(_i_pointer begin, _i_pointer end)
	{
		_i_pointer bufEnd = end;

		end -= (int32_t)Input.size();
		int32_t increment = 1;

		if (Alignment > 1) {
			increment = Alignment;
			if ((begin % Alignment) != 0)
				begin += Alignment - (begin % Alignment);
		}

		if (begin > end)
			return;

		_i_pointer bufBegin = begin;

		const int bufSize = 4096;
		uint8_t   buf[bufSize];
		bool      finished = false;

		while (begin <= end && !finished) {
			_i_pointer wantRead = (bufEnd - bufBegin);
			if (wantRead > bufSize)
				wantRead = bufSize;

			if (!MemoryHelper::ReadBytes((void*)bufBegin, buf, (int32_t)wantRead))
				break;

			_i_pointer tempEnd = bufBegin + wantRead;
			tempEnd -= (int32_t)Input.size();

			while (begin <= tempEnd && !finished) {
				if (TestBytes(&buf[begin - bufBegin])) {
					Result.push_back((void*)begin);
					if (Result.size() >= MaxResults) {
						finished = true;
						break;
					}
				}

				begin += increment;
			}

			bufBegin += bufSize;
		}
	}

	bool TestBytes(uint8_t* buf)
	{
		int32_t len = (int32_t)Input.size();
		for (int32_t i = 0; i < len; i++) {
			uint16_t ch = Input[i];
			if (ch == 0xFFFF)
				continue;

			if (buf[i] != ch)
				return false;
		}

		return true;
	}
};

typedef void (*HookDelegate)(CONTEXT&);

struct HookInfo
{
	static void Execute(void* ptr, void* func, void* ret)
	{
		CONTEXT* ctx = (CONTEXT*)ptr;
		ptr = MemoryHelper::AddPointer(ptr, sizeof(CONTEXT));
		DWORD64* after = (DWORD64*)ptr;
		if (after[0] == 0)
			after = (DWORD64*)MemoryHelper::AddPointer(after, sizeof(void*));
		else if (after[0] == 1)
			after = (DWORD64*)MemoryHelper::AddPointer(after, sizeof(void*) * 2);
		else
			throw std::exception("HookInfo::Execute.1");

		ctx->EFlags = (DWORD)after[0];
		ctx->Rcx = after[1];
		ctx->Rax = after[2];
		ctx->Rsp = after[3];
		ctx->Rip = (DWORD64)ret;

		HookDelegate fn = reinterpret_cast<HookDelegate>(func);
		fn(*ctx);

		RtlRestoreContext(ctx, NULL);
	}
};

struct HookHelper
{
	HookHelper() = delete;

private:
	static void _place(uint8_t* buf, int32_t index, void* address)
	{
		_i_pointer v = (_i_pointer)address;
		uint8_t*   ptr = (uint8_t*)&v;
		for (int32_t i = 0; i < sizeof(void*); i++)
			buf[index + i] = ptr[i];
	}

	static void _place(uint8_t* buf, int32_t index, int32_t value)
	{
		uint8_t* ptr = (uint8_t*)&value;
		for (int32_t i = 0; i < sizeof(int32_t); i++)
			buf[index + i] = ptr[i];
	}

public:
	/// <summary>
	/// Allocate a section of memory that can be executed as code.
	/// </summary>
	/// <param name="size">The size of the block.</param>
	/// <returns></returns>
	static void* AllocateCode(int32_t size)
	{
		if (size <= 0)
			return nullptr;

		if (size > 65536)
			throw std::exception("Trying to allocate code that has size of more than 65536!");

		static void*   CODE_Page = nullptr;
		static int32_t CODE_Size = 0;
		static int32_t CODE_Index = 0;

		if (CODE_Page) {
			int32_t remain = CODE_Size - CODE_Index;
			if (size <= remain) {
				void* result = MemoryHelper::AddPointer(CODE_Page, CODE_Index);
				CODE_Index += size;
				int32_t extra = CODE_Index % 16;
				if (extra != 0)
					CODE_Index += 16 - extra;
				return result;
			}
		}

		CODE_Page = VirtualAlloc(NULL, 65536, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);  // PAGE_EXECUTE_READ ?
		CODE_Size = 65536;
		CODE_Index = 0;

		if (!CODE_Page)
			throw std::exception("Failed to allocate code page!");

		{
			void* result = CODE_Page;
			CODE_Index += size;
			int32_t extra = CODE_Index % 16;
			if (extra != 0)
				CODE_Index += 16 - extra;
			return result;
		}
	}

	/// <summary>
	/// Find a code cave in same segment as address. May return nullptr if not found!
	/// </summary>
	/// <param name="address">The target address to find near.</param>
	/// <param name="length">The minimum length of code cave.</param>
	/// <returns></returns>
	static void* FindCodeCave(void* address, int32_t length = 14)
	{
		MemoryScanner scan;

		if (!MemoryHelper::GetSegmentRange(address, scan.BeginAddress, scan.EndAddress))
			return nullptr;

		scan.Input.push_back(0xC3);
		for (int32_t i = 0; i < length; i++)
			scan.Input.push_back(0xCC);

		scan.MaxResults = 1;
		scan.Scan();

		if (scan.Result.empty())
			return nullptr;

		// Add 1 because we start searching at "ret".
		return MemoryHelper::AddPointer(scan.Result[0], 1);
	}

	/// <summary>
	/// Write an absolute jump from address to target. This will try to write 14 bytes!
	/// </summary>
	/// <param name="address">The address we jump FROM (where we write the jump).</param>
	/// <param name="target">The address we jump to.</param>
	/// <returns></returns>
	static bool WriteAbsoluteJump(void* address, void* target)
	{
		uint8_t buf[14];
		{
			// jmp [rip+0], dq: target
			buf[0] = 0xFF;
			buf[1] = 0x25;
			buf[2] = 0x00;
			buf[3] = 0x00;
			buf[4] = 0x00;
			buf[5] = 0x00;
			_place(buf, 6, target);
		}

		return MemoryHelper::WriteBytes(address, buf, 14);
	}

	/// <summary>
	/// Write a rel jump from address to target. This will try to write 5 bytes! Address and target must be within int32 distance or it will fail.
	/// </summary>
	/// <param name="address">The address we jump FROM (where we write the jump).</param>
	/// <param name="target">The address we jump to.</param>
	/// <returns></returns>
	static bool WriteRelJump(void* address, void* target)
	{
		_i_pointer i_address = (_i_pointer)address;
		i_address += 5;

		_i_pointer i_target = (_i_pointer)target;

		_i_pointer i_diff = i_target - i_address;
		if (abs(i_diff) >= 0x7FFFFFFF)
			return false;

		int32_t t_diff = (int32_t)i_diff;

		uint8_t buf[5];
		{
			buf[0] = 0xE9;  // jmp target
			uint8_t* tbuf = (uint8_t*)&t_diff;
			for (int32_t i = 0; i < 4; i++)
				buf[i + 1] = tbuf[i];
		}

		return MemoryHelper::WriteBytes(address, buf, 5);
	}

	/// <summary>
	/// Write a rel call from address to target. This will try to write 5 bytes! Address and target must be within int32 distance or it will fail.
	/// </summary>
	/// <param name="address">The address we call FROM (where we write the call).</param>
	/// <param name="target">The address we call to.</param>
	/// <returns></returns>
	static bool WriteRelCall(void* address, void* target)
	{
		_i_pointer i_address = (_i_pointer)address;
		i_address += 5;

		_i_pointer i_target = (_i_pointer)target;

		_i_pointer i_diff = i_target - i_address;
		if (abs(i_diff) >= 0x7FFFFFFF)
			return false;

		int32_t t_diff = (int32_t)i_diff;

		uint8_t buf[5];
		{
			buf[0] = 0xE8;  // call target
			uint8_t* tbuf = (uint8_t*)&t_diff;
			for (int32_t i = 0; i < 4; i++)
				buf[i + 1] = tbuf[i];
		}

		return MemoryHelper::WriteBytes(address, buf, 5);
	}

	/// <summary>
	/// Write NOP opcode(s) to address.
	/// </summary>
	/// <param name="address">The address to write to.</param>
	/// <param name="count">The amount of NOPs to write.</param>
	/// <returns></returns>
	static bool WriteNop(void* address, int32_t count)
	{
		if (count <= 0)
			return false;

		uint8_t buf[64];
		memset(buf, 0x90, 64);

		while (true) {
			int32_t len = count;
			if (len > 64)
				len = 64;

			if (!MemoryHelper::WriteBytes(address, buf, len))
				return false;

			count -= len;
			if (count == 0)
				break;

			address = MemoryHelper::AddPointer(address, len);
		}

		return true;
	}

	/// <summary>
	/// Write a hook to specified address.
	/// </summary>
	/// <param name="address">The address to write hook to.</param>
	/// <param name="replaceLength">The replaced instructions length (jump back will be address + replaced). Must be at least 5!</param>
	/// <param name="includeLength">The included code length, if positive then include before our code, if negative then include after our code. If 0 then don't include anything.</param>
	/// <param name="codeBuf">Our code buffer.</param>
	/// <param name="codeLen">Our code length.</param>
	/// <returns></returns>
	static bool WriteHook(void* address, int32_t replaceLength, int32_t includeLength, const uint8_t* codeBuf, int32_t codeLen)
	{
		if (replaceLength < 5)
			return false;

		// The full code size will be the code that we are putting now from buf, included code and also the absolute jump back at end.
		int32_t fullCodeSize = codeLen + abs(includeLength) + 14;

		void* code = AllocateCode(fullCodeSize);
		if (code == nullptr)
			return false;

		int32_t codeIndex = 0;
		if (includeLength > 0) {
			uint8_t* tbuf = (uint8_t*)malloc(includeLength);
			if (!MemoryHelper::ReadBytes(address, tbuf, includeLength)) {
				free(tbuf);
				return false;
			}

			if (!MemoryHelper::WriteBytes(code, tbuf, includeLength)) {
				free(tbuf);
				return false;
			}

			free(tbuf);
			codeIndex = includeLength;
		}

		if (codeLen > 0) {
			if (!MemoryHelper::WriteBytes(MemoryHelper::AddPointer(code, codeIndex), codeBuf, codeLen))
				return false;

			codeIndex += codeLen;
		}

		if (includeLength < 0) {
			includeLength = -includeLength;
			uint8_t* tbuf = (uint8_t*)malloc(includeLength);
			if (!MemoryHelper::ReadBytes(address, tbuf, includeLength)) {
				free(tbuf);
				return false;
			}

			if (!MemoryHelper::WriteBytes(MemoryHelper::AddPointer(code, codeIndex), tbuf, includeLength)) {
				free(tbuf);
				return false;
			}

			free(tbuf);
			codeIndex += includeLength;
		}

		if (!WriteAbsoluteJump(MemoryHelper::AddPointer(code, codeIndex), MemoryHelper::AddPointer(address, replaceLength)))
			return false;

		codeIndex += 14;

		if (codeIndex != fullCodeSize)
			throw std::exception("codeIndex != fullCodeSize");

		auto cave = FindCodeCave(address, 14);
		if (!cave)
			return false;

		if (!WriteAbsoluteJump(cave, code))
			return false;

		if (!WriteRelJump(address, cave))
			return false;

		if (replaceLength > 5)
			WriteNop(MemoryHelper::AddPointer(address, 5), replaceLength - 5);  // Ok to fail even if it shouldn't actually fail.

		return true;
	}

	/// <summary>
	/// Write a hook to specified address.
	/// </summary>
	/// <param name="address">The address to write hook to.</param>
	/// <param name="replaceLength">The replaced instructions length (jump back will be address + replaced). Must be at least 5!</param>
	/// <param name="includeLength">The included code length, if positive then include before our code, if negative then include after our code. If 0 then don't include anything.</param>
	/// <param name="codeBuf">Our code buffer.</param>
	/// <param name="codeLen">Our code length.</param>
	/// <param name="pattern">The pattern we expect at address, it will ensure the bytes first.</param>
	/// <returns></returns>
	static bool WriteHook(void* address, int32_t replaceLength, int32_t includeLength, const uint8_t* codeBuf, int32_t codeLen, std::string_view pattern)
	{
		if (!pattern.empty()) {
			if (!MemoryHelper::TestBytes(address, pattern))
				return false;
		}

		return WriteHook(address, replaceLength, includeLength, codeBuf, codeLen);
	}

	/// <summary>
	/// Write a hook to specified address.
	/// </summary>
	/// <param name="address">The address to write hook to.</param>
	/// <param name="replaceLength">The replaced instructions length (jump back will be address + replaced). Must be at least 5!</param>
	/// <param name="includeLength">The included code length, if positive then include before our code, if negative then include after our code. If 0 then don't include anything.</param>
	/// <param name="func">The hook function that will be called.</param>
	/// <returns></returns>
	static bool WriteHook(void* address, int32_t replaceLength, int32_t includeLength, HookDelegate func)
	{
		if (replaceLength < 5)
			return false;

		int32_t fullCodeSize = 0x59 + abs(includeLength) + 14;

		void* code = AllocateCode(fullCodeSize);
		if (code == nullptr)
			return false;

		int32_t codeIndex = 0;
		if (includeLength > 0) {
			uint8_t* tbuf = (uint8_t*)malloc(includeLength);
			if (!MemoryHelper::ReadBytes(address, tbuf, includeLength)) {
				free(tbuf);
				return false;
			}

			if (!MemoryHelper::WriteBytes(code, tbuf, includeLength)) {
				free(tbuf);
				return false;
			}

			free(tbuf);
			codeIndex = includeLength;
		}

		void* ret = MemoryHelper::AddPointer(address, replaceLength);

		uint8_t mcode[] = {
			/* [ 0] */ 0x54,                                                        // push rsp
			/* [ 1] */ 0x50,                                                        // push rax
			/* [ 2] */ 0x51,                                                        // push rcx
			/* [ 3] */ 0x9C,                                                        // pushf
			/* [ 4] */ 0x48, 0x31, 0xC0,                                            // xor rax, rax
			/* [ 7] */ 0x50,                                                        // push rax
			/* [ 8] */ 0x48, 0x89, 0xE0,                                            // mov rax, rsp
			/* [ b] */ 0x48, 0x83, 0xE0, 0xF0,                                      // and rax, 0xFFFFFFFFFFFFFFF0
			/* [ f] */ 0x48, 0x39, 0xE0,                                            // cmp rax, rsp
			/* [12] */ 0x74, 0x08,                                                  // je continue
			/* [14] */ 0x48, 0xC7, 0xC0, 0x01, 0x00, 0x00, 0x00,                    // mov rax, 1
			/* [1b] */ 0x50,                                                        // push rax
																					/* [1c] continue: */
			/* [1c] */ 0x48, 0x81, 0xEC, 0x99, 0x99, 0x00, 0x00,                    // sub rsp, sizeof(CONTEXT)
			/* [23] */ 0x48, 0x89, 0xE1,                                            // mov rcx, rsp
			/* [26] */ 0x48, 0xB8, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77,  // movabs rax, RtlCaptureContext
			/* [30] */ 0xFF, 0xD0,                                                  // call rax
			/* [32] */ 0x48, 0x89, 0xE1,                                            // mov rcx, rsp
			/* [35] */ 0x48, 0x83, 0xEC, 0x20,                                      // sub rsp, 0x20
			/* [39] */ 0x48, 0xB8, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77,  // movabs rax, HookInfo::Execute
			/* [43] */ 0x48, 0xBA, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77,  // movabs rdx, hook delegate
			/* [4D] */ 0x49, 0xB8, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77,  // movabs r8, ret if include >= 0 or after if include < 0
			/* [57] */ 0xFF, 0xD0                                                   // call rax
			/* [59] */                                                              // after:
		};

		_place(mcode, 0x1C + 3, sizeof(CONTEXT));
		_place(mcode, 0x26 + 2, reinterpret_cast<void*>(&RtlCaptureContext));
		_place(mcode, 0x39 + 2, reinterpret_cast<void*>(&HookInfo::Execute));
		_place(mcode, 0x43 + 2, reinterpret_cast<void*>(func));
		if (includeLength >= 0)
			_place(mcode, 0x4D + 2, ret);
		else {
			void* after = MemoryHelper::AddPointer(code, sizeof(mcode));
			_place(mcode, 0x4D + 2, after);
		}

		if (!MemoryHelper::WriteBytes(MemoryHelper::AddPointer(code, codeIndex), mcode, sizeof(mcode)))
			return false;

		codeIndex += sizeof(mcode);

		if (includeLength < 0) {
			includeLength = -includeLength;
			uint8_t* tbuf = (uint8_t*)malloc(includeLength);
			if (!MemoryHelper::ReadBytes(address, tbuf, includeLength)) {
				free(tbuf);
				return false;
			}

			if (!MemoryHelper::WriteBytes(MemoryHelper::AddPointer(code, codeIndex), tbuf, includeLength)) {
				free(tbuf);
				return false;
			}

			free(tbuf);
			codeIndex += includeLength;
		}

		if (!WriteAbsoluteJump(MemoryHelper::AddPointer(code, codeIndex), MemoryHelper::AddPointer(address, replaceLength)))
			return false;

		codeIndex += 14;

		if (codeIndex != fullCodeSize)
			throw std::exception("codeIndex != fullCodeSize");

		auto cave = FindCodeCave(address, 14);
		if (!cave)
			return false;

		if (!WriteAbsoluteJump(cave, code))
			return false;

		if (!WriteRelJump(address, cave))
			return false;

		if (replaceLength > 5)
			WriteNop(MemoryHelper::AddPointer(address, 5), replaceLength - 5);  // Ok to fail even if it shouldn't actually fail.

		return true;
	}

	/// <summary>
	/// Write a hook to specified address.
	/// </summary>
	/// <param name="address">The address to write hook to.</param>
	/// <param name="replaceLength">The replaced instructions length (jump back will be address + replaced). Must be at least 5!</param>
	/// <param name="includeLength">The included code length, if positive then include before our code, if negative then include after our code. If 0 then don't include anything.</param>
	/// <param name="func">The hook function that will be called.</param>
	/// <param name="pattern">The pattern we expect at address, it will ensure the bytes first.</param>
	/// <returns></returns>
	static bool WriteHook(void* address, int32_t replaceLength, int32_t includeLength, HookDelegate func, std::string_view pattern)
	{
		if (!pattern.empty()) {
			if (!MemoryHelper::TestBytes(address, pattern))
				return false;
		}

		return WriteHook(address, replaceLength, includeLength, func);
	}
};
