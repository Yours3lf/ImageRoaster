#include <iostream>
#include <string>
#include <assert.h>
#include <sstream>

#include "ImageRoaster.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

int main(int argc, char **args)
{
    if (argc < 2)
    {
        std::cerr << "Usage " << args[0] << " [compress/decompress] [inputfile] [outputfile]" << std::endl;
    }

    std::string modeStr;
    if (argc > 1)
    {
        modeStr = std::string(args[1]);
    }

    std::string inputFileStr;
    if (argc > 2)
    {
        inputFileStr = std::string(args[2]);
    }

    std::string outputFileStr;
    if (argc > 3)
    {
        outputFileStr = std::string(args[3]);
    }

    ImageRoaster ir;
    std::vector<uint8_t> resBuf;

    if (modeStr == "compress")
    {
        int w, h, c;
        unsigned char *data = stbi_load(inputFileStr.c_str(), &w, &h, &c, 0);

        ir.compressImage<uint8_t>(resBuf, data, w * h * c * sizeof(uint8_t), 8, c, w, h, 8);

        stbi_image_free(data);
    }
    else if (modeStr == "decompress")
    {
        std::vector<uint8_t> data;
        ir.loadImage(inputFileStr, data);

        uint32_t width, height, bitDepthPerChannel, tileSize, channels;
        ir.getCompressedImageMetadata(data, width, height, bitDepthPerChannel, tileSize, channels);

        std::vector<uint8_t> decompressed;
        ir.decompressImage(data, decompressed);

        // rudimentary pnm header
        std::stringstream ss;

        if (channels < 2)
        {
            // PGM
            ss << "P5" << std::endl;
        }
        else
        {
            // PPM
            ss << "P6" << std::endl;
        }

        ss << width << std::endl
           << height << std::endl
           << ((1 << bitDepthPerChannel) - 1) << std::endl;

        std::string pnmHeader = ss.str();

        resBuf.resize(pnmHeader.size() + decompressed.size());
        memcpy(resBuf.data(), pnmHeader.c_str(), pnmHeader.size());
        memcpy(resBuf.data() + pnmHeader.size(), decompressed.data(), decompressed.size());
    }
    else
    {
        std::cerr << "unknown mode: " << modeStr << std::endl;
        exit(-1);
    }

    ir.saveImage(outputFileStr, resBuf.data(), resBuf.size());

    return 0;
}
