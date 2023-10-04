#include <Windows.h>
#include "stb_image_write.h"
#include "AssetPack.h"
#include "Log.h"

void TestWritePng(const std::string& outDir) {
	std::vector<uint8_t> test_data;
	for (int y = 0; y < 256; y++) {
		for (int x = 0; x < 256; x++) {
			test_data.push_back(50);
			test_data.push_back(100);
			test_data.push_back(200);
			test_data.push_back(255);
		}
	}
	// from: https://stackoverflow.com/questions/9235679/create-a-directory-if-it-doesnt-exist
	if (CreateDirectory(outDir.c_str(), NULL) || ERROR_ALREADY_EXISTS == GetLastError()) {
		std::string fullpath = outDir + "/test.png";
		EXPECT(stbi_write_png(fullpath.c_str(), 256, 256, 4, test_data.data(), 4 * 256) != 0, true)
	} else {
		LOG("failed to create dir")
	}
}

bool ExportAssetPack(const AssetPack& assetPack, const std::string& outDir) {
	TestWritePng(outDir);
	return true;
}
