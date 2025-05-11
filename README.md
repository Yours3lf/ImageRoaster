# ImageRoaster
ImageRoaster is a lossless image compression library.
It works on a very simple principle: 
 1. Split up the image into tiles 
 2. Find the minimum and maximum pixel value within each channel
 3. Calculate required bit depth to represent range between min and max
 4. For each tile, store the required bit depth and min value
 5. For each pixel, store the difference between min value and the pixel value at the per-tile bit depth

## Advantages
 1. It is able to achieve quite a high compression ratio. 
In my limited testing it was able to achieve a 25% compression ratio (2.98MB -> 0.75MB) on a 0.75MPix RGBA8 image. With the same picture:
	 - PNG achieves a 18% ratio (0.54MB).
     - WEBP achieves a 11% ratio (0.33MB)
     - JPEGXL achieves a 11% ratio (0.34MB)
 3. It is incredibly fast, compresses image the above image in ~90 milliseconds on mobile hardware (probably room to improve though)

## Benchmark results
Here's how it stacks up in this lossless image compression benchmark against other algorithms on the same computer using the ImgInfoRGB dataset
https://github.com/WangXuan95/Image-Compression-Benchmark

| Algorithm     | Compression ratio | Encoding time (s) | Decoding time (s) |
| ------------- | ----------------- | ----------------- | ----------------- |
| ImageRoaster  | 64%               | 4.3               | 3.7               |
| fNBLI v0.4    | 41%               | 6.1               | 3.8               |
| HALIC v0.7.2  | 43%               | 2.2               | 2.8               |
| HALIC v0.7.2f | 45%               | 1.6               | 2.1               |
| KVICK         | 43%               | 4.9               | 4.4               |
| QIC v1        | 46%               | 2.3               | 2.4               |
| BIM v0.3      | 41%               | 32.7              | 36.2              |
| QOI           | 71%               | 4.2               | 2.2               |
| QOIR          | 63%               | 13.9              | 13.6              |
| ZPNG -1       | 52%               | 16.7              | 17.4              |
| PNG           | 49%               | 52.1              | 5.0               |
| JPEG-XL -e1   | 48%               | 5.9               | 9.1               |
| WEBP -1       | 44%               | 95.6              | 4.3               |
| WEBP -5       | 44%               | 144.8             | 4.4               |
| JPEG2000      | 46%               | 41.1              | 36.5              |
| JPEG-LS       | 45%               | 12.0              | 9.6               |

## Drawbacks
Compression ratio will suffer if there is smaller than tile sized high frequency detail in the image. However, this is only a big issue if the whole image has high frequency detail as the format is able to adjust the number of bits per pixel for each tile.
