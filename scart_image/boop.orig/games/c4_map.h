const signed char map[7][6][14] =
{
        {
                {    0,  24,  45,  -1,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0 },
                {    4,  24,  25,  49,  -1,   0,   0,   0,   0,   0,   0,   0,   0,   0 },
                {    8,  24,  25,  26,  53,  -1,   0,   0,   0,   0,   0,   0,   0,   0 },
                {   12,  24,  25,  26,  60,  -1,   0,   0,   0,   0,   0,   0,   0,   0 },
                {   16,  25,  26,  64,  -1,   0,   0,   0,   0,   0,   0,   0,   0,   0 },
                {   20,  26,  68,  -1,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0 },
        },
        {
                {    0,   1,  27,  46,  -1,   0,   0,   0,   0,   0,   0,   0,   0,   0 },
                {    4,   5,  27,  28,  45,  50,  -1,   0,   0,   0,   0,   0,   0,   0 },
                {    8,   9,  27,  28,  29,  49,  54,  60,  -1,   0,   0,   0,   0,   0 },
                {   12,  13,  27,  28,  29,  53,  59,  64,  -1,   0,   0,   0,   0,   0 },
                {   16,  17,  28,  29,  63,  68,  -1,   0,   0,   0,   0,   0,   0,   0 },
                {   20,  21,  29,  67,  -1,   0,   0,   0,   0,   0,   0,   0,   0,   0 },
        },
        {
                {    0,   1,   2,  30,  47,  -1,   0,   0,   0,   0,   0,   0,   0,   0 },
                {    4,   5,   6,  30,  31,  46,  51,  60,  -1,   0,   0,   0,   0,   0 },
                {    8,   9,  10,  30,  31,  32,  45,  50,  55,  59,  64,  -1,   0,   0 },
                {   12,  13,  14,  30,  31,  32,  49,  54,  58,  63,  68,  -1,   0,   0 },
                {   16,  17,  18,  31,  32,  53,  62,  67,  -1,   0,   0,   0,   0,   0 },
                {   20,  21,  22,  32,  66,  -1,   0,   0,   0,   0,   0,   0,   0,   0 },
        },
        {
                {    0,   1,   2,   3,  33,  48,  60,  -1,   0,   0,   0,   0,   0,   0 },
                {    4,   5,   6,   7,  33,  34,  47,  52,  59,  64,  -1,   0,   0,   0 },
                {    8,   9,  10,  11,  33,  34,  35,  46,  51,  56,  58,  63,  68,  -1 },
                {   12,  13,  14,  15,  33,  34,  35,  45,  50,  55,  57,  62,  67,  -1 },
                {   16,  17,  18,  19,  34,  35,  49,  54,  61,  66,  -1,   0,   0,   0 },
                {   20,  21,  22,  23,  35,  53,  65,  -1,   0,   0,   0,   0,   0,   0 },
        },
        {
                {    1,   2,   3,  36,  59,  -1,   0,   0,   0,   0,   0,   0,   0,   0 },
                {    5,   6,   7,  36,  37,  48,  58,  63,  -1,   0,   0,   0,   0,   0 },
                {    9,  10,  11,  36,  37,  38,  47,  52,  57,  62,  67,  -1,   0,   0 },
                {   13,  14,  15,  36,  37,  38,  46,  51,  56,  61,  66,  -1,   0,   0 },
                {   17,  18,  19,  37,  38,  50,  55,  65,  -1,   0,   0,   0,   0,   0 },
                {   21,  22,  23,  38,  54,  -1,   0,   0,   0,   0,   0,   0,   0,   0 },
        },
        {
                {    2,   3,  39,  58,  -1,   0,   0,   0,   0,   0,   0,   0,   0,   0 },
                {    6,   7,  39,  40,  57,  62,  -1,   0,   0,   0,   0,   0,   0,   0 },
                {   10,  11,  39,  40,  41,  48,  61,  66,  -1,   0,   0,   0,   0,   0 },
                {   14,  15,  39,  40,  41,  47,  52,  65,  -1,   0,   0,   0,   0,   0 },
                {   18,  19,  40,  41,  51,  56,  -1,   0,   0,   0,   0,   0,   0,   0 },
                {   22,  23,  41,  55,  -1,   0,   0,   0,   0,   0,   0,   0,   0,   0 },
        },
        {
                {    3,  42,  57,  -1,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0 },
                {    7,  42,  43,  61,  -1,   0,   0,   0,   0,   0,   0,   0,   0,   0 },
                {   11,  42,  43,  44,  65,  -1,   0,   0,   0,   0,   0,   0,   0,   0 },
                {   15,  42,  43,  44,  48,  -1,   0,   0,   0,   0,   0,   0,   0,   0 },
                {   19,  43,  44,  52,  -1,   0,   0,   0,   0,   0,   0,   0,   0,   0 },
                {   23,  44,  56,  -1,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0 },
        }
};

const signed char drop_order[7] =
{
	3, 4, 2, 5, 1, 6, 0
};
