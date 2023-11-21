//
// Created by miyehn on 11/21/2023.
//
#include "Log.h"
#include "cxxopts.hpp"
#include "AssetPack.h"

int main(int argc, const char* argv[]) {

	cxxopts::Options options("EchoesExporter", "Repository link: https://github.com/miyehn/echoes-exporter\nDocumentation: https://docs.google.com/document/d/1baNgrlB1rL4iv3bOv5UMPJFIhPZEiBO-PChzEj8PPOw/edit#heading=h.w762mki0fcfe\n");
	options.add_options()
		("f,file", "input psd file, absolute path or relative to executable", cxxopts::value<std::string>()/*->default_value("input.psd")*/)
		("o,outdir", "output directory/assetpack name, absolute path or relative to executable", cxxopts::value<std::string>()/*->default_value("output")*/)
		("c,clean", "whether the existing output directory should be cleaned away first", cxxopts::value<int>()->default_value("0"))
		;
	options.allow_unrecognised_options();

	auto result = options.parse(argc, argv);

	std::string inFile, outDir;
	int cleanFirst;
	try {
		inFile = result["file"].as<std::string>();
		outDir = result["outdir"].as<std::string>();
		cleanFirst = result["clean"].as<int>();
		LOG("in: %s, out: %s", inFile.c_str(), outDir.c_str())
	} catch (std::exception &e) {
		std::cout << options.help() << std::endl;
		return 0;
	}

	//////////////////////////////////////////////////

#if 1
	AssetPack assetPack;
	if (!EchoesReadPsd(inFile, assetPack)) {
		return 1;
	}

	ExportAssetPack(assetPack, outDir, cleanFirst);

#endif

	LOG("done.")
	return 0;
}
