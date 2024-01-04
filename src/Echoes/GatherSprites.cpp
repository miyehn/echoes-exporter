//
// Created by miyehn on 12/27/2023.
//
// import
#include <regex>
#include <filesystem>

#include "../Psd/Psd.h"
#include "../Psd/PsdMallocAllocator.h"
#include "../Psd/PsdNativeFile.h"
#include "../Psd/PsdLayer.h"
#include "../Psd/PsdExport.h"
#include "../Psd/PsdExportDocument.h"

#include "Log.h"
#include "Utils.h"
#include "AssetPack.h"

#include "cxxopts.hpp"
#include "../External/stb_image.h"
#include "json/json.h"

bool AddSprite(psd::ExportDocument* document, psd::Allocator &allocator, const std::string& spritePath, const std::string& pngPath, int left, int top, int maxSize=1024) {
	////////////////////////////////////////////////////////////////
	// load png
	int nativeChannels = 0;
	int iWidth = 0; int iHeight = 0;
	uint8_t* pixels = stbi_load(pngPath.c_str(), &iWidth, &iHeight, &nativeChannels, 4);

	if (iWidth > maxSize || iHeight > maxSize) {
		WARN("Skipping sprite '%s' because it's too big (%ix%i)", spritePath.c_str(), iWidth, iHeight)
		return false;
	}

	uint32_t numPixels = iWidth * iHeight;
	std::vector<uint8_t> channelR, channelG, channelB, channelA;
	channelR.resize(numPixels);
	channelG.resize(numPixels);
	channelB.resize(numPixels);
	channelA.resize(numPixels);
	for (int i = 0; i < numPixels; i++) {
		channelR[i] = pixels[i * 4 + 0];
		channelG[i] = pixels[i * 4 + 1];
		channelB[i] = pixels[i * 4 + 2];
		channelA[i] = pixels[i * 4 + 3];
	}

	// add a separator here to end the sprite name (makes parsing easier)
	const unsigned int layer1 = AddLayer(document, &allocator, (spritePath + "#").c_str());

	UpdateLayer(document, &allocator, layer1, psd::exportChannel::RED, left, top, left + iWidth, top + iHeight, channelR.data(), psd::compressionType::RAW);
	UpdateLayer(document, &allocator, layer1, psd::exportChannel::GREEN, left, top, left + iWidth, top + iHeight, channelG.data(), psd::compressionType::RAW);
	UpdateLayer(document, &allocator, layer1, psd::exportChannel::BLUE, left, top, left + iWidth, top + iHeight, channelB.data(), psd::compressionType::RAW);
	UpdateLayer(document, &allocator, layer1, psd::exportChannel::ALPHA, left, top, left + iWidth, top + iHeight, channelA.data(), psd::compressionType::RAW);

	LOG("loaded '%s' w=%d, h=%d", spritePath.c_str(), iWidth, iHeight)
	return true;
}

bool EchoesGatherSprites(const std::vector<std::tuple<std::string, std::string, Json::Value>>& spritesToLoad, const std::string& outFilePath) {
	const std::string outPsd = outFilePath + ".psd";
	const std::wstring fullPath(outPsd.c_str(), outPsd.c_str() + outPsd.length());
	psd::MallocAllocator allocator;
	psd::NativeFile file(&allocator);

	// try opening the file. if it fails, bail out.
	if (!file.OpenWrite(fullPath.c_str()))
	{
		ERR("failed to open file for write")
		return false;
	}

	psd::ExportDocument* document = CreateExportDocument(&allocator, 2048, 2048, 8u, psd::exportColorMode::RGB);
	AddMetaData(document, &allocator, "author", "miyehn");

	Json::Value indexRoot;

	/////////////////////

	int spriteIdx = 0;
	for (const auto &sprite: spritesToLoad) {
		auto& spritePath = std::get<0>(sprite);
		auto& pngPath = std::get<1>(sprite);

		AddSprite(document, allocator, spritePath, pngPath, 0, 0);

		Json::Value spriteInfo;
		spriteInfo["spritePath"] = spritePath;
		spriteInfo["pivot"] = std::get<2>(sprite);
		indexRoot[spriteIdx] = spriteInfo;
		spriteIdx++;
	}

	WriteDocument(document, &allocator, &file);
	DestroyExportDocument(document, &allocator);
	file.Close();
	LOG("done writing .psd")

	/////////////////////
	std::stringstream stream;
	stream << indexRoot;
	if (WriteStringToFile(outFilePath + ".index", stream.str())) {
		LOG("done writing .index")
		return true;
	}
	return false;
}

int main(int argc, const char* argv[]) {

	cxxopts::Options options("EchoesGatherSprites", "Documentation: TODO\n");
	options.allow_unrecognised_options();

#if 1
	options.add_options()
		("d,directory", "directory to iterate, absolute path or relative to executable", cxxopts::value<std::string>()/*->default_value("input.psd")*/)
		("o,output", "output name (psd & index), absolute path or relative to executable", cxxopts::value<std::string>()/*->default_value("output")*/)
		("x,ignore", "sprite path ignore list (regex, one per line)", cxxopts::value<std::string>()/*->default_value("output")*/)
		("i,include", "include list (regex, one per line)", cxxopts::value<std::string>()/*->default_value("output")*/)
		("l,listonly", "list sprites only; don't create psd", cxxopts::value<bool>())
		;
	auto result = options.parse(argc, argv);

	std::string inDirectory, outPsd, ignoreListFile, includeListFile;
	bool listOnly;
	try {
		inDirectory = result["directory"].as<std::string>();
		outPsd = result["output"].as<std::string>();
		ignoreListFile = result["ignore"].as<std::string>();
		includeListFile = result["include"].as<std::string>();
		listOnly = result["listonly"].as<bool>();
		LOG("dir: %s, out: %s, ignore: %s, include: %s", inDirectory.c_str(), outPsd.c_str(), ignoreListFile.c_str(), includeListFile.c_str())
	} catch (std::exception &e) {
		std::cout << options.help() << std::endl;
		return 0;
	}

	std::vector<std::regex> ignoreList;
	{// read from ignoreListFile to populate
		std::vector<std::string> list = SplitLines(ReadFileAsString(ignoreListFile));
		for (const auto &line: list) {
			if (!line.empty() && line[0] != '#') ignoreList.emplace_back(line);
		}
	}

	std::vector<std::regex> includeList;
	{// and whitelist
		std::vector<std::string> list = SplitLines(ReadFileAsString(includeListFile));
		for (const auto &line: list) {
			if (!line.empty() && line[0] != '#') includeList.emplace_back(line);
		}
	}

	std::vector<std::tuple<std::string, std::string, Json::Value>> spritesToLoad;
	{// populate spritesToLoad
		Json::Reader reader;
		std::filesystem::path assetPacksRoot(inDirectory);
		for (const auto& dirEntry : std::filesystem::recursive_directory_iterator(inDirectory)) {
			if (dirEntry.path().extension() == ".assetpack") {
				std::string pngPathBase = dirEntry.path().parent_path().string();
				std::string spritePathBase = std::filesystem::relative(dirEntry.path(), assetPacksRoot).parent_path().string();
				std::replace(spritePathBase.begin(), spritePathBase.end(), '\\', '/');
				if (!pngPathBase.empty()) pngPathBase += "/";
				if (!spritePathBase.empty()) spritePathBase += "/";

				std::string assetPack = ReadFileAsString(dirEntry.path().string());
				Json::Value root;
				if (reader.parse(assetPack, root)) {
					std::map<std::string, Json::Value> pivotsMap;
					for (const auto &pivot: root["pivots"]) {
						pivotsMap[pivot["texPath"].asString()] = pivot["pivot"];
					}
					for (const auto &mat: root["materials"]) {
						const std::string spritePath = spritePathBase + mat["name"].asString();
						const std::string localMainTexPath = mat["mainTexPath"].asString();
						const std::string pngPath = pngPathBase + localMainTexPath;
						Json::Value pivot = pivotsMap[localMainTexPath];

						bool ignored = false;
						// ignore list
						for (const auto &ignore: ignoreList) {
							if (std::regex_match(spritePath, ignore)) {
								ignored = true;
								break;
							}
						}
						// include list
						if (ignored)
						for (const auto &include: includeList) {
							if (std::regex_match(spritePath, include)) {
								ignored = false;
								break;
							}
						}

						LOG("%s%s", ignored ? "\t[ignored] " : "", spritePath.c_str())
						if (!ignored) {
							spritesToLoad.emplace_back(spritePath, pngPath, pivot);
						}
					}
				} else {
					ERR("failed to parse json!")
				}
			}
		}
		LOG("======== %zu sprites ========", spritesToLoad.size())
	}
	// with all the sprites to load, actually load them:
	if (listOnly) {
		LOG("(list only; skipping actual output..)")
	} else {
		LOG("(writing psd '%s')", outPsd.c_str())
		EchoesGatherSprites(spritesToLoad, outPsd);
	}

#endif

	LOG("done.")
	return 0;
}
