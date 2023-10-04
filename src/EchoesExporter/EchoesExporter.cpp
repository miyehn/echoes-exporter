//
// Created by miyehn on 10/3/2023.
//

#include <vector>
#include <string>

#include "Log.h"
#include "cxxopts.hpp"
#include "stb_image_write.h"

#include "../Psd/Psd.h"
#include "../Psd/PsdPlatform.h"

int main(int argc, const char* argv[]) {

	cxxopts::Options options("EchoesExporter", "todo: put documentation link here");
	options.add_options()
		("f,file", "input psd file, abs path or relative to executable", cxxopts::value<std::string>()/*->default_value("input.psd")*/)
		("o,outdir", "output directory, abs path or relative to executable", cxxopts::value<std::string>()->default_value("output"))
		("h,help", "print help message")
		;
	options.allow_unrecognised_options();

	auto result = options.parse(argc, argv);

	std::string inFile, outDir;
	try {
		inFile = result["file"].as<std::string>();
		outDir = result["outdir"].as<std::string>();
		LOG("in: %s, out: %s", inFile.c_str(), outDir.c_str())
	} catch (std::exception &e) {
		std::cout << options.help() << std::endl;
		return 0;
	}

	//////////////////////////////////////////////////

	std::vector<uint8_t> test_data;
	for (int y = 0; y < 256; y++) {
		for (int x = 0; x < 256; x++) {
			test_data.push_back(50);
			test_data.push_back(100);
			test_data.push_back(200);
			test_data.push_back(255);
		}
	}
	//std::string out_dir = PROJECT_DIR;
	//out_dir += "/bin/output/";
	// from: https://stackoverflow.com/questions/9235679/create-a-directory-if-it-doesnt-exist
	if (CreateDirectory(outDir.c_str(), NULL) || ERROR_ALREADY_EXISTS == GetLastError()) {
		std::string fullpath = outDir + "/test.png";
		EXPECT(stbi_write_png(fullpath.c_str(), 256, 256, 4, test_data.data(), 4 * 256) != 0, true)
	} else {
		LOG("failed to create dir")
	}

	LOG("done")
	return 0;
}