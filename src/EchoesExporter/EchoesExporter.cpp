//
// Created by miyehn on 10/3/2023.
//

#include "Log.h"
#include "cxxopts.hpp"

#include "../Psd/Psd.h"
#include "../Psd/PsdMallocAllocator.h"
#include "../Psd/PsdLayerMaskSection.h"
#include "../Psd/PsdNativeFile.h"
#include "../Psd/PsdDocument.h"
#include "../Psd/PsdParseDocument.h"
#include "../Psd/PsdColorMode.h"
#include "../Psd/PsdLayer.h"
#include "../Psd/PsdParseLayerMaskSection.h"
#include "../Psd/PsdLayerCanvasCopy.h"
#include "../Psd/PsdChannel.h"
#include "../Psd/PsdInterleave.h"
#include "../Psd/PsdChannelType.h"
#include "../Psd/PsdLayerType.h"

#include "AssetPack.h"

// helpers for reading PSDs
namespace
{
PSD_USING_NAMESPACE;
static const unsigned int CHANNEL_NOT_FOUND = UINT_MAX;


// ---------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------
template <typename T, typename DataHolder>
static void* ExpandChannelToCanvas(Allocator* allocator, const DataHolder* layer, const void* data, unsigned int canvasWidth, unsigned int canvasHeight)
{
	T* canvasData = static_cast<T*>(allocator->Allocate(sizeof(T)*canvasWidth*canvasHeight, 16u));
	memset(canvasData, 0u, sizeof(T)*canvasWidth*canvasHeight);

	imageUtil::CopyLayerData(static_cast<const T*>(data), canvasData, layer->left, layer->top, layer->right, layer->bottom, canvasWidth, canvasHeight);

	return canvasData;
}


// ---------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------
static void* ExpandChannelToCanvas(const Document* document, Allocator* allocator, Layer* layer, Channel* channel)
{
	if (document->bitsPerChannel == 8)
		return ExpandChannelToCanvas<uint8_t>(allocator, layer, channel->data, document->width, document->height);
	else if (document->bitsPerChannel == 16)
		return ExpandChannelToCanvas<uint16_t>(allocator, layer, channel->data, document->width, document->height);
	else if (document->bitsPerChannel == 32)
		return ExpandChannelToCanvas<float32_t>(allocator, layer, channel->data, document->width, document->height);

	return nullptr;
}


// ---------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------
template <typename T>
static void* ExpandMaskToCanvas(const Document* document, Allocator* allocator, T* mask)
{
	if (document->bitsPerChannel == 8)
		return ExpandChannelToCanvas<uint8_t>(allocator, mask, mask->data, document->width, document->height);
	else if (document->bitsPerChannel == 16)
		return ExpandChannelToCanvas<uint16_t>(allocator, mask, mask->data, document->width, document->height);
	else if (document->bitsPerChannel == 32)
		return ExpandChannelToCanvas<float32_t>(allocator, mask, mask->data, document->width, document->height);

	return nullptr;
}


// ---------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------
unsigned int FindChannel(Layer* layer, int16_t channelType)
{
	for (unsigned int i = 0; i < layer->channelCount; ++i)
	{
		Channel* channel = &layer->channels[i];
		if (channel->data && channel->type == channelType)
			return i;
	}

	return CHANNEL_NOT_FOUND;
}


// ---------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------
template <typename T>
T* CreateInterleavedImage(Allocator* allocator, const void* srcR, const void* srcG, const void* srcB, unsigned int width, unsigned int height)
{
	T* image = static_cast<T*>(allocator->Allocate(width*height * 4u*sizeof(T), 16u));

	const T* r = static_cast<const T*>(srcR);
	const T* g = static_cast<const T*>(srcG);
	const T* b = static_cast<const T*>(srcB);
	imageUtil::InterleaveRGB(r, g, b, T(0), image, width, height);

	return image;
}


// ---------------------------------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------
template <typename T>
T* CreateInterleavedImage(Allocator* allocator, const void* srcR, const void* srcG, const void* srcB, const void* srcA, unsigned int width, unsigned int height)
{
	T* image = static_cast<T*>(allocator->Allocate(width*height * 4u*sizeof(T), 16u));

	const T* r = static_cast<const T*>(srcR);
	const T* g = static_cast<const T*>(srcG);
	const T* b = static_cast<const T*>(srcB);
	const T* a = static_cast<const T*>(srcA);
	imageUtil::InterleaveRGBA(r, g, b, a, image, width, height);

	return image;
}
}



bool ReadDocument(const std::string& inFile, AssetPack& assetPack) {

	const std::wstring fullPath(inFile.c_str(), inFile.c_str() + inFile.length());
	psd::MallocAllocator allocator;
	psd::NativeFile file(&allocator);

	// open file
	if (!file.OpenRead(fullPath.c_str())) {
		ERR("failed to open file")
		return false;
	}

	// create document
	psd::Document* document = psd::CreateDocument(&file, &allocator);
	if (!document) {
		ERR("failed to create psd document")
		file.Close();
		return false;
	}

	// check color mode
	if (document->colorMode != psd::colorMode::RGB) {
		ERR("psd is not in RGB color mode")
		file.Close();
		return false;
	}

	// check bits per channel
	if (document->bitsPerChannel != 8) {
		ERR("this psd doesn't have 8 bits per channel")
		file.Close();
		return false;
	}

	// read section
	auto section = psd::ParseLayerMaskSection(document, &file, &allocator);
	if (!section) {
		ERR("failed to parse layer mask section")
		file.Close();
		return false;
	}

	// layers
	for (int i = 0; i < section->layerCount; i++) {
		psd::Layer* layer = &section->layers[i];
		psd::ExtractLayer(document, &file, &allocator, layer);

		if (layer->type == layerType::ANY) {
			const unsigned int indexR = FindChannel(layer, channelType::R);
			const unsigned int indexG = FindChannel(layer, channelType::G);
			const unsigned int indexB = FindChannel(layer, channelType::B);
			const unsigned int indexA = FindChannel(layer, channelType::TRANSPARENCY_MASK);
			const bool allChannelsFound =
				indexR!=CHANNEL_NOT_FOUND &&
				indexG!=CHANNEL_NOT_FOUND &&
				indexB!=CHANNEL_NOT_FOUND &&
				indexA!=CHANNEL_NOT_FOUND;

			if (!allChannelsFound) {
				ERR("some layer channels were not found!")
				file.Close();
				return false;
			}

			void* canvasData[4] = {
				ExpandChannelToCanvas(document, &allocator, layer, &layer->channels[indexR]),
				ExpandChannelToCanvas(document, &allocator, layer, &layer->channels[indexG]),
				ExpandChannelToCanvas(document, &allocator, layer, &layer->channels[indexB]),
				ExpandChannelToCanvas(document, &allocator, layer, &layer->channels[indexA]),
			};
			uint8_t* layerData = CreateInterleavedImage<uint8_t>(
				&allocator, canvasData[0], canvasData[1], canvasData[2], canvasData[3], document->width, document->height);
			allocator.Free(canvasData[0]);
			allocator.Free(canvasData[1]);
			allocator.Free(canvasData[2]);
			allocator.Free(canvasData[3]);

			allocator.Free(layerData);
		}
	}

	file.Close();
	return section;
}


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

	AssetPack assetPack;
	if (!ReadDocument(inFile, assetPack)) {
		return 1;
	}

	ExportAssetPack(assetPack, outDir);

	LOG("done.")
	return 0;
}