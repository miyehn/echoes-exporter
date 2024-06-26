#pragma once

#include <string>
#include <vector>
#include "json/json-forwards.h"

#if ECHOES_EXPORTER_WINDOWED
enum GUILogType: int {
	LT_LOG = 0,
	LT_SUCCESS = 1,
	LT_WARNING = 2,
	LT_ERROR = 3
};
struct GUILogEntry {
	GUILogType type;
	std::string msg;
};
void AppendToGUILog(const GUILogEntry &entry, bool clearFirst = false);
#endif

// trim from start (in place)
inline void ltrim(std::string &s) {
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
		return !std::isspace(ch);
	}));
}

// trim from end (in place)
inline void rtrim(std::string &s) {
	s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
		return !std::isspace(ch);
	}).base(), s.end());
}

// trim from both ends (in place)
inline void trim(std::string &s) {
	rtrim(s);
	ltrim(s);
}

inline std::string tolower(const std::string& s) {
	std::string result;
	for (char c : s) {
		result += std::tolower(c);
	}
	return result;
}

std::string ReadFileAsString(const std::string& path);

bool WriteStringToFile(const std::string& path, const std::string& content);

std::vector<std::string> SplitLines(const std::string& str);

namespace psd {
	struct Document;
	struct LayerMaskSection;
	struct MallocAllocator;
	struct NativeFile;
	struct Layer;
}
bool ReadPsdLayers(
	const std::string& inFile,
	psd::Document*& pDocument,
	psd::LayerMaskSection*& pSection,
	psd::MallocAllocator& allocator,
	psd::NativeFile& file);

std::string GetName(const psd::Layer* layer);

bool IsOrUnderLayer(const psd::Layer* layer, const std::string& ancestorName);

bool VisibleInHierarchy(const psd::Layer* layer);

struct vec2 {
	float x = 0;
	float y = 0;

	vec2 operator+(const vec2 other) const {
		return {
			x + other.x,
			y + other.y
		};
	}

	vec2 operator-(const vec2 other) const {
		return {
			x - other.x,
			y - other.y
		};
	}

	vec2 operator*(float c) const {
		return { x * c, y * c};
	}
	vec2 operator/(float c) const {
		return { x / c, y / c};
	}
	Json::Value serialized() const;
};

struct ivec2 {
	int x = 0;
	int y = 0;
	operator vec2() const {
		return {(float)x, (float)y};
	}
};

const float sqrt2 = std::sqrt(2.0f);
const float sqrt3 = std::sqrt(3.0f);
const vec2 ISO_X = {sqrt2 / 2, sqrt2 / sqrt3 / 2 };
const vec2 ISO_Y = {sqrt2 / 2, -sqrt2 / sqrt3 / 2 };
const float STANDARD_PPDU = sqrt2 * 100;

vec2 PixelPosToUnitPos(vec2 pixelPos, float workingPPDU = STANDARD_PPDU);

vec2 UnitPosToPixelPos(vec2 unitPos, float workingPPDU = STANDARD_PPDU);

std::vector<std::string> SplitTokens(const std::string& s, char separator=' ');

std::string JoinTokens(const std::vector<std::string> &tokens);
