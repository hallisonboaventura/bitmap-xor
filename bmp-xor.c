#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#define BITMAP_INFO_HEADER_SIZE  40
#define BITMAP_INFO_HEADER_START 14

#define GET_PADDING(N) N % 4 ? N + (4 - (N % 4)) : N

typedef struct {
    uint16_t type;
    uint32_t size;
    uint32_t reserved;
    uint32_t offset_bits;
} bitmap_file_header_t;

// v3
typedef struct {
    uint32_t size;
    int32_t  px_width;
    int32_t  px_height;
    uint16_t planes;
    uint16_t bit_count;
    uint32_t compression;
    uint32_t image_size;  // in bytes
    int32_t  x_pixels_per_meter;
    int32_t  y_pixels_per_meter;
    uint32_t colors_used; // 2 ^ bit_count
    uint32_t color_important;
} bitmap_info_header_t;

typedef struct {
    uint8_t blue;
    uint8_t green;
    uint8_t red;
    uint8_t reserved;
} color_definition_t;

int32_t main(int32_t argc, int8_t *argv[])
{
    if (argc != 4) {
        printf("usage:\nxor %%s %%s %%s, input bitmap, one time pad key, output bitmap\n");
        return 1;
    }

    FILE *fin = fopen(argv[1], "r");
    if (!fin) {
        fprintf(stderr, "%s\n", strerror(errno));
        return 2;
    }

    uint32_t header_size = 0;
    fseek(fin, BITMAP_INFO_HEADER_START, SEEK_SET);
    fread(&header_size, sizeof(uint32_t), 1, fin);

    if (header_size != BITMAP_INFO_HEADER_SIZE) {
        fprintf(
            stderr,
            "Information header size found: %u bytes\n"
            "Only 40 bytes header is supported\n",
            header_size
        );

        fclose(fin);
        return 3;
    }

    bitmap_file_header_t file_header;
    bitmap_info_header_t info_header;
    memset(&file_header, 0, sizeof(bitmap_file_header_t));
    memset(&info_header, 0, sizeof(bitmap_info_header_t));

    fseek(fin, 0, SEEK_SET);
    fread(&file_header.type,        sizeof(uint16_t), 1, fin);
    fread(&file_header.size,        sizeof(uint32_t), 1, fin);
    fread(&file_header.reserved,    sizeof(uint32_t), 1, fin);
    fread(&file_header.offset_bits, sizeof(uint32_t), 1, fin);

    // É suportado chamar apenas um fread, pois
    // sizeof(bitmap_info_header_t) é multiplo de 8 (sem padding)
    fread(&info_header, 1, sizeof(bitmap_info_header_t), fin);

    if (info_header.bit_count != 1) {
        fprintf(
            stderr,
            "Bit depth found: %u bits\n"
            "Only 1 bit depth is supported\n",
            info_header.bit_count
        );

        fclose(fin);
        return 4;
    }

    // Lendo tabela de cores
    color_definition_t indexed_color[info_header.colors_used];
    memset(indexed_color, 0, info_header.colors_used * sizeof(color_definition_t));
    fread(indexed_color, sizeof(color_definition_t), info_header.colors_used, fin);

    // Esse cálculo é válido apenas para 1 bit
    // Arredondamento da divisão para cima
    uint32_t raw_data_width = (info_header.px_width + (8 - 1)) / 8;
    uint32_t data_table_width = GET_PADDING(raw_data_width);

    // Prepara leitura da chave para memória
    FILE *fkey = fopen(argv[2], "rb");
    if (!fkey) {
        fprintf(stderr, "%s\n", strerror(errno));

        fclose(fin);
        return 5;
    }

    fseek(fkey, 0, SEEK_END);
    long int key_size = ftell(fkey);
    uint32_t image_data_size = raw_data_width * info_header.px_height;

    if (key_size != image_data_size) {
        fprintf(
            stderr,
            "Key data size: %ld bytes\n"
            "Image data size: %u bytes (without padding)\n"
            "Size of key data must be equal of image data table size (without padding)\n",
            key_size, image_data_size
        );

        fclose(fkey);
        fclose(fin);
        return 6;
    }

    fseek(fkey, 0, SEEK_SET);
    uint8_t key_block[raw_data_width];
    memset(key_block, 0, raw_data_width * sizeof(uint8_t));

    // Começa o processo de escrita no arquivo de saída
    FILE *fout = fopen(argv[3], "w");
    if (!fout) {
        fprintf(stderr, "%s\n", strerror(errno));

        fclose(fkey);
        fclose(fin);
        return 7;
    }

    // Cabeçalho do arquivo
    fwrite(&file_header.type,        sizeof(uint16_t), 1, fout);
    fwrite(&file_header.size,        sizeof(uint32_t), 1, fout);
    fwrite(&file_header.reserved,    sizeof(uint32_t), 1, fout);
    fwrite(&file_header.offset_bits, sizeof(uint32_t), 1, fout);

    // Cabeçalho das informações da imagem
    fwrite(&info_header, sizeof(bitmap_info_header_t), 1, fout);

    // Tabela de índices de cores
    fwrite(indexed_color, sizeof(color_definition_t), info_header.colors_used, fout);

    uint8_t padded_row[data_table_width];
    memset(padded_row, 0, data_table_width * sizeof(uint8_t));

    uint32_t height, j;
    for (height = info_header.px_height; height-- > 0;) {
        fread(key_block,  sizeof(uint8_t), raw_data_width,   fkey);
        fread(padded_row, sizeof(uint8_t), data_table_width, fin);

        // Aplicando xor em cada byte "visível"
        for (j = 0; j < raw_data_width; ++j) {
            padded_row[j] ^= key_block[j];
        }

        fwrite(padded_row, sizeof(uint8_t), data_table_width, fout);
    }

    fclose(fout);
    fclose(fkey);
    fclose(fin);

    return 0;
}
