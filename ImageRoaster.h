#pragma once

#include <vector>
#include <chrono>
#include <iostream>
#include <fstream>

// Compressing an image exploiting spatial locality
// we split the WxH image with C channels with B bits per channel into NxN tiles
// we store tile size in 8 bits once per image (eg. 2x2, 4x4, 8x8, 12x12, 16x16, 20x20, 24x24, 28x28, 32x32)
// for each tile:
//	for each channel:
//		find min/max values for channel within tile
//		we store min as full B bit value once per channel
//		we calc the diff between min/max and the req bit depth, store it in the next 4bits (eg. 1, 2, 4, 8, 16)
//		for each pixel we just store the diff between mini value and current pixel

// Compressing an image stream exploiting temporal coherence:
// Store seq number
// 0 means full frame
// Anything >0 only stores the diff from tile seq 0
// If diff between current tile and seq 0 becomes too big (eg bigger bit per pixel requirement than tile seq 0), reset seq to 0 and set full frame

// TODO:
//-implement temporal compression

class ImageRoaster
{
	static const bool profiling = true;

public:
	template <typename T>
	void compareImages(const T *imageA, const T *imageB, uint32_t width, uint32_t height, uint32_t channels) const
	{
		for (uint32_t y = 0; y < height; ++y)
		{
			for (uint32_t x = 0; x < width; ++x)
			{
				for (uint32_t c = 0; c < channels; ++c)
				{
					T pixelA = imageA[(y * width + x) * channels + c];
					T pixelB = imageB[(y * width + x) * channels + c];

					if (pixelA != pixelB)
					{
						std::cerr << "Pixel mismatch at: (" << x << ", " << y << "):" << c << " [ " << pixelA << " != " << pixelB << " ]" << std::endl;
					}
				}
			}
		}
	}

	void saveImage(const std::string &filename, const uint8_t *image, uint32_t length) const
	{
		std::ofstream f;
		f.open(filename, std::ios::binary);
		f.write((const char *)image, length);
	}

	void decompressImage(const std::vector<uint8_t> &buf, std::vector<uint8_t> &resBuf)
	{
		auto start = std::chrono::system_clock::now();

		uint32_t *ptr32 = (uint32_t *)buf.data();
		uint16_t *ptr16 = (uint16_t *)buf.data();

		uint32_t width = ptr32[0];
		uint32_t height = ptr32[1];

		uint32_t bitDepthPerChannel = ((ptr16[4]) & 0x3f) + 1;
		uint32_t tileSize = ((ptr16[4] >> 6) & 0x3f) + 1;
		uint32_t channels = ((ptr16[4] >> 12) & 0xf) + 1;

		assert(tileSize >= 4);

		uint32_t tileOffset = 2 * sizeof(uint32_t) + sizeof(uint16_t);

		auto decompressCore = [&]<typename T>()
		{
			T *pixels = (T *)resBuf.data();

			for (uint32_t y = 0; y < height; y += tileSize)
			{
				for (uint32_t x = 0; x < width; x += tileSize)
				{
					for (uint32_t c = 0; c < channels; ++c)
					{
						uint8_t metadata = *(uint8_t *)(buf.data() + tileOffset);
						uint32_t tileBpp = (metadata & 0x3f) + 1;
						bool allTilePixelsSame = (metadata >> 6) & 0x1;
						bool fullOrDiffFrame = (metadata >> 7) & 0x1;

						T maxVal = (uint32_t(1) << tileBpp) - 1;

						tileOffset += sizeof(uint8_t);

						T minValue = 0;
						if (sizeof(T) * 8 != tileBpp || allTilePixelsSame)
						{
							minValue = *(T *)(buf.data() + tileOffset);
							tileOffset += sizeof(T);
						}

						auto loadTileSameBpp = [&]<typename D>()
						{
							uint32_t counter = 0;
							for (uint32_t yy = 0, yb = y; yy < tileSize && yb < height; ++yy, ++yb)
							{
								for (uint32_t xx = 0, xb = x; xx < tileSize && xb < width; ++xx, ++xb)
								{
									D currData = *(D *)(buf.data() + tileOffset + (counter >> 3));

									pixels[(yb * width + xb) * channels + c] = currData;

									counter += tileBpp;
								}
							}
						};

						if (allTilePixelsSame)
						{
							for (uint32_t yy = 0, yb = y; yy < tileSize && yb < height; ++yy, ++yb)
							{
								for (uint32_t xx = 0, xb = x; xx < tileSize && xb < width; ++xx, ++xb)
								{
									pixels[(yb * width + xb) * channels + c] = minValue;
								}
							}
						}
						else
						{
							if (sizeof(T) * 8 != tileBpp)
							{
								uint32_t counter = 0;
								for (uint32_t yy = 0, yb = y; yy < tileSize && yb < height; ++yy, ++yb)
								{
									for (uint32_t xx = 0, xb = x; xx < tileSize && xb < width; ++xx, ++xb)
									{
										T pixelDiff = 0;

										uint32_t bitsLeftToRead = tileBpp;

										while (bitsLeftToRead > 0)
										{
											uint8_t currByte = *(uint8_t *)(buf.data() + tileOffset + (counter >> 3));

											uint32_t bitsToRead = std::min(bitsLeftToRead, 8 - (counter % 8));
											uint32_t bitsToReadMask = (uint32_t(1) << bitsToRead) - 1;

											pixelDiff |= ((currByte >> (counter % 8)) & bitsToReadMask) << (tileBpp - bitsLeftToRead);

											counter += bitsToRead;
											bitsLeftToRead -= bitsToRead;
										}

										T pixel = minValue + (pixelDiff & maxVal);
										pixels[(yb * width + xb) * channels + c] = pixel;
									}
								}
							}
							else
							{
								if (tileBpp <= 8)
								{
									loadTileSameBpp.template operator()<uint8_t>();
								}
								else
								{
									loadTileSameBpp.template operator()<uint16_t>();
								}
							}

							uint32_t tileSizeBits = (tileSize * tileSize * tileBpp);
							tileOffset += (tileSizeBits >> 3) + ((tileSizeBits % 8) > 0);
						}
					}
				}
			}
		};

		if (bitDepthPerChannel <= 8)
		{
			resBuf.resize(width * height * channels);

			decompressCore.template operator()<uint8_t>();
		}
		else
		{
			resBuf.resize(width * height * channels * 2);

			decompressCore.template operator()<uint16_t>();
		}

		auto end = std::chrono::system_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
		if (profiling)
		{
			std::cout << "DeCompression elapsed time: " << elapsed << std::endl;
		}
	}

	template <typename T>
	void compressImage(std::vector<uint8_t> &resBuf, const T *data, uint32_t length, uint32_t bitDepthPerChannel, uint32_t channels, uint32_t width, uint32_t height, uint32_t tileSize)
	{
		assert(width * height * channels * sizeof(T) == length);
		assert(tileSize >= 4);
		assert(data);
		assert(bitDepthPerChannel <= 64);

		auto start = std::chrono::system_clock::now();

		resBuf.reserve(width * height * channels * sizeof(T) + 5 * sizeof(uint32_t));

		// write out metadata
		resBuf.resize(2 * sizeof(uint32_t) + sizeof(uint16_t));
		uint32_t *ptr32 = (uint32_t *)resBuf.data();
		uint16_t *ptr16 = (uint16_t *)resBuf.data();
		ptr32[0] = width;
		ptr32[1] = height;

		ptr16[4] = (bitDepthPerChannel - 1) & 0x3f | // 6 bits: 1...64
				   ((tileSize - 1) & 0x3f) << 6 |	 // 6 bits: 4...64
				   ((channels - 1) & 0xf) << 12;	 // 4 bits  1...16

		const T *pixels = (const T *)data;

		// we cache tile pixels during minmax value search so that we don't pay cache misses twice
		// accessing the rows of a tile
		std::vector<T> tilePixels;
		tilePixels.resize(tileSize * tileSize * channels);

		// seems to help the compiler realise it should really pull some memory into cache
		std::vector<T> strideCache;
		strideCache.resize(tileSize * channels);

		for (uint32_t y = 0; y < height; y += tileSize)
		{
			for (uint32_t x = 0; x < width; x += tileSize)
			{
				// figure out smallest and largest value per channel
				T minValue[channels];
				T maxValue[channels];
				for (uint32_t c = 0; c < channels; ++c)
				{
					minValue[c] = ~T(0);
					maxValue[c] = 0;
				}

				for (uint32_t yy = 0, yb = y; yy < tileSize && yb < height; ++yy, ++yb)
				{
					memcpy(strideCache.data(), &pixels[(yb * width + x) * channels + 0], strideCache.size() * sizeof(T));
					for (uint32_t xx = 0, xb = x; xx < tileSize && xb < width; ++xx, ++xb)
					{
						for (uint32_t c = 0; c < channels; ++c)
						{
							T pixel = strideCache[xx * channels + c];

							tilePixels[(yy * tileSize + xx) * channels + c] = pixel;

							minValue[c] = std::min(pixel, minValue[c]);
							maxValue[c] = std::max(pixel, maxValue[c]);
						}
					}
				}

				for (uint32_t c = 0; c < channels; c++)
				{
					uint32_t tileBpp = 0;
					{
						uint32_t diff = maxValue[c] - minValue[c];

						while (diff != 0)
						{
							diff >>= 1;
							tileBpp++;
						}

						if (tileBpp == 0)
							tileBpp = 1;

						auto nextPow2 = [](uint32_t v)
						{
							v--;
							v |= v >> 1;
							v |= v >> 2;
							v |= v >> 4;
							v |= v >> 8;
							v |= v >> 16;
							v++;
							return v;
						};

						// tileBpp = nextPow2(tileBpp);
					}

					T maxVal = (uint32_t(1) << tileBpp) - 1;

					uint32_t currOffset = resBuf.size();
					resBuf.resize(resBuf.size() + sizeof(uint8_t));

					bool allTilePixelsSame = minValue[c] == maxValue[c];
					bool fullOrDiffFrame = false; // TODO

					*(uint8_t *)(resBuf.data() + currOffset) =
						((tileBpp - 1) & 0x3f) |		   // bpp stored in lower 6 bits
						((allTilePixelsSame & 0x1) << 6) | // all tile pixels are the same flag 1 bit
						((fullOrDiffFrame & 0x1) << 7);	   // reserved for full frame or diff frame 1 bit

					currOffset += sizeof(uint8_t);

					// only write out minValue if input BPP != output BPP
					// OR all pixels the same (we need still need one)
					if ((sizeof(T) * 8) != tileBpp || allTilePixelsSame)
					{
						resBuf.resize(resBuf.size() + sizeof(T));
						*(T *)(resBuf.data() + currOffset) = minValue[c];
						currOffset += sizeof(T);
					}

					// skip writing out any more values if all tile pixels are the same
					if (allTilePixelsSame)
						continue;

					// allocate tile pixels
					uint32_t tileSizeBits = (tileSize * tileSize * tileBpp);
					uint32_t offset = resBuf.size();
					resBuf.resize(resBuf.size() + (tileSizeBits >> 3) + ((tileSizeBits % 8) > 0));
					memset(resBuf.data() + offset, 0, resBuf.size() - offset);

					if ((sizeof(T) * 8) != tileBpp)
					{
						uint32_t counter = 0;
						for (uint32_t yy = 0, yb = y; yy < tileSize && yb < height; ++yy, ++yb)
						{
							for (uint32_t xx = 0, xb = x; xx < tileSize && xb < width; ++xx, ++xb)
							{
								T pixel = tilePixels[(yy * tileSize + xx) * channels + c];
								T pixelDiff = (pixel - minValue[c]) & maxVal;

								uint32_t bitsLeftToWrite = tileBpp;

								while (bitsLeftToWrite > 0)
								{
									uint8_t *currByte = (uint8_t *)(resBuf.data() + currOffset + (counter >> 3));

									uint32_t bitsToWrite = std::min(bitsLeftToWrite, 8 - (counter % 8));
									uint32_t bitsToWriteMask = (uint32_t(1) << bitsToWrite) - 1;

									uint8_t dataToStore = (pixelDiff & bitsToWriteMask) << (counter % 8);

									*currByte |= dataToStore;

									counter += bitsToWrite;
									bitsLeftToWrite -= bitsToWrite;
									pixelDiff >>= bitsToWrite;
								}
							}
						}
					}
					else
					{
						auto storeTileSameBpp = [&]<typename D>()
						{
							uint32_t counter = 0;
							for (uint32_t yy = 0, yb = y; yy < tileSize && yb < height; ++yy, ++yb)
							{
								for (uint32_t xx = 0, xb = x; xx < tileSize && xb < width; ++xx, ++xb)
								{
									T pixel = tilePixels[(yy * tileSize + xx) * channels + c];

									D *currData = (D *)(resBuf.data() + currOffset + (counter >> 3));

									*currData = pixel;

									counter += tileBpp;
								}
							}
						};

						if (tileBpp <= 8)
						{
							storeTileSameBpp.template operator()<uint8_t>();
						}
						else
						{
							storeTileSameBpp.template operator()<uint16_t>();
						}
					}
				}
			}
		}

		auto end = std::chrono::system_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

		if (profiling)
		{
			std::cout << "Total elapsed time: " << elapsed << std::endl;
			uint32_t inputSize = (width * height * channels * (bitDepthPerChannel <= 8 ? 1 : 2));
			std::cout << "Input size: " << inputSize << " bytes" << std::endl;
			std::cout << "Output size: " << resBuf.size() << " bytes" << std::endl;
			std::cout << "Compression ratio: " << uint32_t(float(resBuf.size()) / float(inputSize) * 100.0f) << "% of original" << std::endl;
		}
	}
};