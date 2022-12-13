#include <stdio.h>
#include <stdlib.h>

#include "bmp.h"

int main(int argc, char *argv[])
{
    // ensure proper usage
    if (argc != 4)
    {
        fprintf(stderr, "Usage: resize n infile outfile\n");
        return 1;
    }

    // read the scaling factor
    float f = atof(argv[1]);
    if (f <= 0 || f > 1)
    {
        fprintf(stderr, "f, the resize factor, must be between 0 and 1.\n");
        return 1;
    }
    char *infile = argv[2];
    char *outfile = argv[3];

    // open input file
    FILE *inptr = fopen(infile, "r");
    if (inptr == NULL)
    {
        fprintf(stderr, "Could not open %s.\n", infile);
        return 2;
    }

    // open output file
    FILE *outptr = fopen(outfile, "w");
    if (outptr == NULL)
    {
        fclose(inptr);
        fprintf(stderr, "Could not create %s.\n", outfile);
        return 3;
    }

    // read infile's BITMAPFILEHEADER
    BITMAPFILEHEADER bf;
    fread(&bf, sizeof(BITMAPFILEHEADER), 1, inptr);

    // read infile's BITMAPINFOHEADER
    BITMAPINFOHEADER bi;
    fread(&bi, sizeof(BITMAPINFOHEADER), 1, inptr);

    // ensure infile is (likely) a 24-bit uncompressed BMP 4.0
    if (bf.bfType != 0x4d42 || bf.bfOffBits != 54 || bi.biSize != 40 ||
        bi.biBitCount != 24 || bi.biCompression != 0)
    {
        fclose(outptr);
        fclose(inptr);
        fprintf(stderr, "Unsupported file format.\n");
        return 4;
    }

    BITMAPFILEHEADER bf_resize = bf;
    BITMAPINFOHEADER bi_resize = bi;
    bi_resize.biWidth = bi.biWidth * f;
    bi_resize.biHeight = bi.biHeight * f;
    int padding = bi.biWidth % 4; // you can simplify the calculation
    int padding_resize = bi_resize.biWidth % 4;
    bi_resize.biSizeImage = (bi_resize.biWidth * sizeof(RGBTRIPLE) + padding_resize) * bi_resize.biHeight;
    bf_resize.bfSize = bi_resize.biSizeImage + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    // allocate mamory for the rgb triplets of the original (input) image
    RGBTRIPLE *pix = malloc(sizeof(RGBTRIPLE) * bi.biWidth * bi.biHeight);
    if (pix == NULL)
    {
        fprintf(stderr, "malloc failed.\n");
        return 5;
    }
    // temporary storage
    RGBTRIPLE triple;

    // read the entire pixels of the original image and store into the memory
    for (int i = 0; i < bi.biHeight; i++)
    {
        for (int j = 0; j < bi.biWidth; j++)
        {
            fread(&triple, sizeof(RGBTRIPLE), 1, inptr);
            pix[i * bi.biWidth + j] = triple;
        }
        // skip over padding, if any
        fseek(inptr, padding, SEEK_CUR);
    }

    // write outfile's header
    fwrite(&bf_resize, sizeof(BITMAPFILEHEADER), 1, outptr);
    fwrite(&bi_resize, sizeof(BITMAPINFOHEADER), 1, outptr);

    // write the pixels of destination (resized) image
    for (int i = 0; i < bi_resize.biHeight; i++)
    {
        for (int j = 0; j < bi_resize.biWidth; j++)
        {
            // calculate the corresponding coorinates in the original image
            int m = (i / f + 0.5); // +0.5 for rounding
            if (m > bi.biHeight - 1)
            { // limit the value
                m = bi.biHeight - 1;
            }
            int n = (j / f + 0.5);
            if (n > bi.biWidth - 1)
            {
                n = bi.biWidth - 1;
            }
            // pick the pixel value at the coordinate
            triple = pix[m * bi.biWidth + n];
            // write RGB triplet to outfile
            fwrite(&triple, sizeof(RGBTRIPLE), 1, outptr);
        }
        // padding for the output image, if any
        for (int j = 0; j < padding_resize; j++)
        {
            fputc(0x00, outptr);
        }
    }
    free(pix);
    fclose(inptr);
    fclose(outptr);

    return 0;
}