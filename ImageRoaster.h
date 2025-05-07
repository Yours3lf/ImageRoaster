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

// TODO:
// Compressing an image stream exploiting temporal coherence:
// Store seq number
// 0 means full frame
// Anything >0 only stores the diff from tile seq 0
// If diff between current tile and seq 0 becomes too big (eg bigger bit per pixel requirement than tile seq 0), reset seq to 0 and set full frame

// TODO:
//-pack metadata more
//-if can't compress tile (ie. output bpp = input bpp), don't write out minvalue, just write out bpp and full tile data
//-if min=max then just write out special value, so we only need to write minvalue
//-implement phase 2 where instead of next pow2 bit depths are used, exact bit depths are used
//-get rid of 2x2 mode. too much overhead

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

		uint32_t width = ptr32[0];
		uint32_t height = ptr32[1];
		uint32_t channels = ptr32[2];
		uint32_t bitDepthPerChannel = ptr32[3];
		uint32_t tileSize = ptr32[4];

		uint32_t tileOffset = 5 * sizeof(uint32_t);

		auto decompressCore = [&]<typename T>()
		{
			T *pixels = (T *)resBuf.data();

			for (uint32_t y = 0; y < height; y += tileSize)
			{
				for (uint32_t x = 0; x < width; x += tileSize)
				{
					for (uint32_t c = 0; c < channels; ++c)
					{
						T minValue = *(T *)(buf.data() + tileOffset);
						uint8_t tileBpp = ((*(uint8_t *)(buf.data() + tileOffset + sizeof(T))) & 0xf) + 1;

						T maxVal = (uint32_t(1) << tileBpp) - 1;

						if (tileSize == 2 && tileBpp == 1)
						{
							uint8_t tile = ((*(uint8_t *)(buf.data() + tileOffset + sizeof(T))) & 0xf0) >> 4;

							uint32_t counter = 0;
							for (uint32_t yy = 0, yb = y; yy < tileSize && yb < height; ++yy, ++yb)
							{
								for (uint32_t xx = 0, xb = x; xx < tileSize && xb < width; ++xx, ++xb)
								{
									uint8_t pixel = minValue + (tile >> counter++) & maxVal;
									pixels[(yb * width + xb) * channels + c] = pixel;
								}
							}

							tileOffset += sizeof(T) + sizeof(uint8_t);
						}
						else
						{
							auto decompressTile = [&]<typename D>()
							{
								uint32_t counter = 0;
								for (uint32_t yy = 0, yb = y; yy < tileSize && yb < height; ++yy, ++yb)
								{
									for (uint32_t xx = 0, xb = x; xx < tileSize && xb < width; ++xx, ++xb)
									{
										D currData = *(D *)(buf.data() + tileOffset + sizeof(T) + sizeof(uint8_t) + (counter >> 3));
										D diff = (currData >> (counter % (sizeof(D) * 8))) & maxVal;
										T pixel = minValue + diff;
										pixels[(yb * width + xb) * channels + c] = pixel;

										counter += tileBpp;
									}
								}
							};

							if (tileBpp <= 8)
							{
								decompressTile.template operator()<uint8_t>();
							}
							else
							{
								decompressTile.template operator()<uint16_t>();
							}

							tileOffset += sizeof(T) + sizeof(uint8_t) + ((tileSize * tileSize * tileBpp) >> 3);
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

	template<typename T>
	void compressImage(std::vector<uint8_t> &resBuf, const T *data, uint32_t bitDepthPerChannel, uint32_t channels, uint32_t width, uint32_t height, uint32_t tileSize)
	{
		auto start = std::chrono::system_clock::now();

		resBuf.reserve(width * height * channels * (bitDepthPerChannel <= 8 ? 1 : 2) + 5 * sizeof(uint32_t));

		// write out metadata
		resBuf.resize(5 * sizeof(uint32_t));
		uint32_t *ptr32 = (uint32_t *)resBuf.data();
		ptr32[0] = width;
		ptr32[1] = height;
		ptr32[2] = channels;
		ptr32[3] = bitDepthPerChannel;
		ptr32[4] = tileSize;

		std::chrono::microseconds minmaxSearchTime = std::chrono::microseconds(0);
		std::chrono::microseconds storeTime = std::chrono::microseconds(0);

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
				auto minmaxSearchStart = std::chrono::system_clock::now();

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

				auto minmaxSearchEnd = std::chrono::system_clock::now();
				minmaxSearchTime += std::chrono::duration_cast<std::chrono::microseconds>(minmaxSearchEnd - minmaxSearchStart);

				auto storeStart = std::chrono::system_clock::now();

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

						tileBpp = nextPow2(tileBpp);
					}

					T maxVal = (uint32_t(1) << tileBpp) - 1;

					uint32_t currSize = resBuf.size();
					if (tileBpp == 1 && tileSize == 2)
					{
						resBuf.resize(currSize + sizeof(uint8_t) + sizeof(T));
					}
					else
					{
						resBuf.resize(currSize + ((tileSize * tileSize * tileBpp) >> 3) + sizeof(uint8_t) + sizeof(T));
					}

					*(T *)(resBuf.data() + currSize) = minValue[c];
					*(uint8_t *)(resBuf.data() + currSize + sizeof(T)) = (tileBpp - 1) & 0xf; // bpp stored in lower 4 bits

					// we support 2x2 tiles if we store the req bit depth in 4 bits
					// and in case of a 1bpp tile store the tile in the next 4 bits
					// as a special case...
					if (tileBpp == 1 && tileSize == 2)
					{
						uint8_t *tileData = (uint8_t *)(resBuf.data() + currSize + sizeof(T));

						uint32_t counter = 0;
						for (uint32_t yy = 0, yb = y; yy < tileSize && yb < height; ++yy, ++yb)
						{
							for (uint32_t xx = 0, xb = x; xx < tileSize && xb < width; ++xx, ++xb)
							{
								// T pixel = pixels[(yb * width + xb) * channels + c];
								T pixel = tilePixels[(yy * tileSize + xx) * channels + c];
								uint8_t pixelDiff = pixel - minValue[c];
								uint8_t dataToStore = (pixelDiff & maxVal) << (4 + counter++);
								*tileData = *tileData | dataToStore; // store data in upper 4 bits
							}
						}
					}
					else
					{
						auto compressTile = [&]<typename D>()
						{
							uint32_t counter = 0;
							for (uint32_t yy = 0, yb = y; yy < tileSize && yb < height; ++yy, ++yb)
							{
								for (uint32_t xx = 0, xb = x; xx < tileSize && xb < width; ++xx, ++xb)
								{
									// T pixel = pixels[(yb * width + xb) * channels + c];
									T pixel = tilePixels[(yy * tileSize + xx) * channels + c];
									T pixelDiff = pixel - minValue[c];

									D *currData = (D *)(resBuf.data() + currSize + sizeof(T) + sizeof(uint8_t) + (counter >> 3));

									if (counter % (sizeof(D) * 8) == 0)
									{
										*currData = 0;
									}

									D dataToStore = D(pixelDiff & maxVal) << (counter % (sizeof(D) * 8));

									*currData = *currData | dataToStore;

									counter += tileBpp;
								}
							}
						};

						if (tileBpp <= 8)
						{
							compressTile.template operator()<uint8_t>();
						}
						else
						{
							compressTile.template operator()<uint16_t>();
						}
					}
				}

				auto storeEnd = std::chrono::system_clock::now();
				storeTime += std::chrono::duration_cast<std::chrono::microseconds>(storeEnd - storeStart);
			}
		}

		auto end = std::chrono::system_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

		if (profiling)
		{
			std::cout << "Min/max search elapsed time: " << minmaxSearchTime << std::endl;
			std::cout << "Store elapsed time: " << storeTime << std::endl;
			std::cout << "Compression elapsed time: " << elapsed << std::endl;
			uint32_t inputSize = (width * height * channels * (bitDepthPerChannel <=8 ? 1 : 2));
			std::cout << "Input size: " << inputSize << " bytes" << std::endl;
			std::cout << "Output size: " << resBuf.size() << " bytes" << std::endl;
			std::cout << "Compression ratio: " << uint32_t(float(resBuf.size()) / float(inputSize) * 100.0f) << "% of original" << std::endl;
		}
	}
};