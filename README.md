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
In my limited testing it was able to achieve a 25% compression ratio (2.98MB -> 0.75MB).
 In the same picture:
 - PNG achieves a 32% ratio (0.96MB).
 - WEBP achieves a 22% ratio (0.66MB)
 - JPEGXL achieves a 19% ratio (0.58MB)
 2. It is incredibly fast, compresses images in milliseconds on mobile hardware

## Drawbacks
Compression ratio will suffer if there is smaller than tile sized high frequency detail in the image. However, this is only a big issue if the whole image has high frequency detail as the format is able to adjust the number of bits per pixel for each tile.
