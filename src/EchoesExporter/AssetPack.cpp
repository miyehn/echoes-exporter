#include <Windows.h>
#include "stb_image_resize.h"
#include "stb_image_write.h"
#include "AssetPack.h"
#include "Log.h"

#define EXPORT_PPU 100.0f

const float sqrt2 = std::sqrt(2.0f);
const vec2 ISO_X = {sqrt2 / 2, 0.8f * sqrt2 / 2 };
const vec2 ISO_Y = {sqrt2 / 2, -0.8f * sqrt2 / 2 };

vec2 ToIsometric(vec2 p) {
	vec2 v0 = ISO_X;
	vec2 v1 = ISO_Y;
	return {
		v0.x * p.x + v1.x * p.y,
		v0.y * p.x + v1.y * p.y
	};
}

vec2 FromIsometric(vec2 p) {
	const float det = ISO_X.x * ISO_Y.y - ISO_Y.x * ISO_X.y;
	vec2 v0 = {
		ISO_Y.y / det,
		-ISO_X.y / det
	};
	vec2 v1 = {
		-ISO_Y.x / det,
		ISO_X.x / det
	};
	return {
		v0.x * p.x + v1.x * p.y,
		v0.y * p.x + v1.y * p.y
	};
}

vec2 PixelPosToUnitPos(vec2 pixelPos, float workingPPDU) {
	vec2 unitPos = FromIsometric(pixelPos);
	return unitPos / (workingPPDU / sqrt(2.0f));
}

vec2 UnitPosToPixelPos(vec2 unitPos, float workingPPDU) {
	vec2 pixelPos = ToIsometric(unitPos);
	return pixelPos * (workingPPDU / sqrt(2.0f));
}

std::vector<std::string> SplitTokens(const std::string& s) {
	std::vector<std::string> tokens;
	std::string cur;
	for (char c : s) {
		if (c == ' ') {
			if (cur.length() > 0) tokens.push_back(cur);
			cur = "";
		} else {
			cur += c;
		}
	}
	if (cur.length() > 0) tokens.push_back(cur);
	return tokens;
}

std::string JoinTokens(const std::vector<std::string> &tokens) {
	std::string result;
	for (auto token : tokens) {
		ASSERT(token.length() > 0)
		token[0] = (char)toupper(token[0]);
		result += token;
	}
	return result;
}

void WritePngToDirectory(
	const std::vector<uint8_t>& data,
	uint32_t width, uint32_t height,
	const std::string& outDir,
	const std::string& fileName,
	float resizeRatio = 1.0f
) {
	// from: https://stackoverflow.com/questions/9235679/create-a-directory-if-it-doesnt-exist
	if (CreateDirectory(outDir.c_str(), NULL) || ERROR_ALREADY_EXISTS == GetLastError()) {
		std::string fullpath = outDir + "/" + fileName;

		// resize
		uint32_t newWidth = std::ceil(width * resizeRatio);
		uint32_t newHeight = std::ceil(height * resizeRatio);
		std::vector<uint8_t> resizedData(newWidth * newHeight * 4);
		stbir_resize_uint8(
			data.data(), width, height, width * 4,
			resizedData.data(), newWidth, newHeight, newWidth * 4, 4);

		// write
		EXPECT(stbi_write_png(fullpath.c_str(), newWidth, newHeight, 4, resizedData.data(), 4 * newWidth) != 0, true)
	} else {
		ERR("failed to create dir")
	}
}

std::vector<uint8_t> Crop(const std::vector<uint8_t>& data, uint32_t srcStrideInBytes, ivec2 minPx, ivec2 sizePx) {
	std::vector<uint8_t> result(sizePx.x * sizePx.y * 4);
	uint32_t dstStrideInBytes = sizePx.x * 4;
	for (int i = 0; i < sizePx.y; i++) {
		uint32_t srcStart = (minPx.y + i) * srcStrideInBytes + minPx.x * 4;
		uint32_t dstStart = i * dstStrideInBytes;
		memcpy(result.data() + dstStart, data.data() + srcStart, dstStrideInBytes);
	}
	return result;
}

bool ExportAssetPack(const AssetPack& assetPack, const std::string& outDir) {

	// compute resize ratio
	const float standardPPDU = std::sqrt(2.0f) * EXPORT_PPU;
	const float resizeRatio = standardPPDU / assetPack.pixelsPerDiagonalUnit;
	LOG("PPDU: %.3f, resize ratio: %.3f", assetPack.pixelsPerDiagonalUnit, resizeRatio)

	for (auto& pair : assetPack.spriteSets) {

		const SpriteSet& sprite = pair.second;
		std::string baseName = JoinTokens(SplitTokens(sprite.name));
		vec2 basePointUnit = sprite.minUnit + sprite.sizeUnit/2;
		vec2 relAnchorPx = UnitPosToPixelPos(basePointUnit, assetPack.pixelsPerDiagonalUnit);
		vec2 anchorPx = relAnchorPx + assetPack.docOriginPx - sprite.minPx;
		vec2 anchorNormalized = {
			anchorPx.x / sprite.sizePx.x,
			1.0f - anchorPx.y / sprite.sizePx.y // since unity wants this reversed
		};
		LOG("%s: (%.3f, %.3f)", baseName.c_str(), anchorNormalized.x, anchorNormalized.y)

		for (int i = 0; i < sprite.baseLayersData.size(); i++) {
			auto baseRegionData = Crop(sprite.baseLayersData[i], assetPack.docWidth * 4, sprite.minPx, sprite.sizePx);
			WritePngToDirectory(
				baseRegionData, sprite.sizePx.x, sprite.sizePx.y, outDir,
				baseName+"_part"+std::to_string(i)+"_base.png", resizeRatio);
		}
		for (int i = 0; i < sprite.lightLayersData.size(); i++) {
			auto lightTexRegionData = Crop(sprite.lightLayersData[i], assetPack.docWidth * 4, sprite.minPx, sprite.sizePx);
			WritePngToDirectory(
				lightTexRegionData, sprite.sizePx.x, sprite.sizePx.y, outDir,
				baseName+"_L" + std::to_string(i) +".png", resizeRatio);
		}
	}
	return true;
}
