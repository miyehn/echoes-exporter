#pragma once

#include <vector>
#include <string>

struct ivec2 {
	int x = 0;
	int y = 0;
};

struct vec2 {
	float x = 0;
	float y = 0;
};

struct SpriteSet {
	std::string name;
	uint8_t* baseLayerData;
	std::vector<uint8_t*> lightLayersData;
	// rel to doc canvas, in pixels
	ivec2 minPx;
	ivec2 sizePx;
	// rel to doc origin, in units (parsed from base layer name)
	vec2 minUnit;
	vec2 sizeUnit;
};

struct AssetPack {
	std::vector<SpriteSet> spriteSets;
	vec2 docOriginPx;
	int docWidth, docHeight;
	float pixelsPerDiagonalUnit;
};

bool ExportAssetPack(const AssetPack& assetPack, const std::string& outDir);