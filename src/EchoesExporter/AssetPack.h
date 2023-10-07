#pragma once

#include <vector>
#include <unordered_map>
#include <string>
#include "json/json-forwards.h"

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

struct SpriteSet {
	std::string name;
	std::vector<std::vector<uint8_t>> baseLayersData;
	std::vector<std::vector<uint8_t>> lightLayersData;
	// rel to doc canvas, in pixels
	ivec2 minPx;
	ivec2 sizePx;
	// rel to doc origin, in units (parsed from base layer name)
	vec2 minUnit;
	vec2 sizeUnit;

	std::string getBaseName() const;
	std::string getBaseTexPath(int index) const;
	std::string getLightTexPath(int index) const;
};

struct AssetPack {
	std::unordered_map<std::string, SpriteSet> spriteSets;
	vec2 docOriginPx;
	uint32_t docWidth, docHeight;
	float pixelsPerDiagonalUnit; // sqrt(2) = 1.41421356237
};

std::vector<std::string> SplitTokens(const std::string& s);

bool ExportAssetPack(const AssetPack& assetPack, const std::string& outDir);