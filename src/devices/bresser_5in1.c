/** @file
    Decoder for Bresser Weather Center 5-in-1.

    Copyright (C) 2018 Daniel Krueger

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

/**
Decoder for Bresser Weather Center 7-in-1, indoor and outdoor sensor.

Preamble:

    aa aa aa aa aa 2d d4

Observed length depends on reset_limit.

Indoor sensor:

    {240}6ab0197005fd29000000000022965700003c000007000000000000000000

    CHK:8h8h ID?8h8h8h8h FLAGS:4h BATT:1b CH:3d 8h 8h8h 8h8h TEMP:12h 4h HUM:8h 8h8h CHK?8h 8h 8h8h TRAILER?8h8h 8h8h 8h8h 8h8h 8h

Outdoor sensor:

    {271}631d05c09e9a18abaabaaaaaaaaa8adacbacff9cafcaaaaaaa000000000000000000

- Data whitening of 0xaa

    CHK:8h8h ID?8h8h WDIR:8h4h° 4h 8h WGUST:8h.4h WAVG:8h.4h RAIN?8h8h RAIN?8h8h TEMP:8h.4hC 4h HUM:8h% LIGHT:8h4h,4hKL ?:8h8h4h TRAILER:8h8h8h4h
    Unit of light is kLux (not W/m²).

First two bytes are likely an LFSR-16 digest, generator 0x8810 with some unknown/variable key?
*/

#include "decoder.h"

static int bresser_7in1_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = {0xaa, 0xaa, 0xaa, 0x2d, 0xd4};
    uint8_t const whiten_pattern[] = {0xaa, 0xaa, 0x00, 0x00};

    data_t *data;
    //uint8_t msg[21];
    uint8_t msg[25];

    bitbuffer_print(bitbuffer);

    if (bitbuffer->num_rows != 1 || bitbuffer->bits_per_row[0] < 240-80) {
        if (decoder->verbose > 1)
            fprintf(stderr, "%s: to few bits (%u)\n", __func__, bitbuffer->bits_per_row[0]);
        return DECODE_ABORT_LENGTH; // unrecognized
    }

    unsigned start_pos = bitbuffer_search(bitbuffer, 0, 0,
            preamble_pattern, sizeof(preamble_pattern) * 8);
    start_pos += sizeof(preamble_pattern) * 8;

    if (start_pos >= bitbuffer->bits_per_row[0]) {
        if (decoder->verbose > 1)
            fprintf(stderr, "%s: preamble not found\n", __func__);
        return DECODE_ABORT_EARLY; // no preamble found
    }
    //if (start_pos + sizeof (msg) * 8 >= bitbuffer->bits_per_row[0]) {
    if (start_pos + 21*8 >= bitbuffer->bits_per_row[0]) {
        if (decoder->verbose > 1)
            fprintf(stderr, "%s: message too short (%u)\n", __func__, bitbuffer->bits_per_row[0] - start_pos);
        return DECODE_ABORT_LENGTH; // message too short
    }

    bitbuffer_extract_bytes(bitbuffer, 0, start_pos, msg, sizeof (msg) * 8);
    bitrow_printf(msg, sizeof (msg) * 8, "MSG: ");

    if (msg[21] != 0x00) {
        // data whitening
        for (unsigned i = 0; i < sizeof (msg); ++i) {
            msg[i] ^= 0xaa;
        }
        bitrow_printf(msg, sizeof (msg) * 8, "XOR: ");

        int chk      = (msg[0] << 8) | (msg[1]);
        int id       = (msg[2] << 8) | (msg[3]);
        int wdir     = (msg[4] >> 4) * 100 + (msg[4] & 0x0f) * 10 + (msg[5] >> 4);
        int wgst_raw = (msg[7] >> 4) * 100 + (msg[7] & 0x0f) * 10 + (msg[8] >> 4);
        int wavg_raw = (msg[8] & 0x0f) * 100 + (msg[9] >> 4) * 10 + (msg[9] & 0x0f);
        int temp_raw = (msg[14] >> 4) * 100 + (msg[14] & 0x0f) * 10 + (msg[15] >> 4);
        //if (msg[25] & 0x0f)
        //    temp_raw = -temp_raw;
        float temp_c = temp_raw * 0.1f;
        int humidity = (msg[16] >> 4) * 10 + (msg[16] & 0x0f);
        int lght_raw = (msg[17] >> 4) * 1000 + (msg[17] & 0x0f) * 100 + (msg[18] >> 4) * 10 + (msg[18] & 0x0f);

        float light_klx = lght_raw * 0.1f;

        /* clang-format off */
        data = data_make(
                "model",            "",             DATA_STRING, "Bresser-7in1outdoor",
                "id",               "",             DATA_INT,    id,
                "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
                "humidity",         "Humidity",     DATA_INT,    humidity,
                "wind_max_m_s",     "Wind Gust",    DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, wgst_raw * 0.1f,
                "wind_avg_m_s",     "Wind Speed",   DATA_FORMAT, "%.1f m/s", DATA_DOUBLE, wavg_raw * 0.1f,
                "wind_dir_deg",     "Direction",    DATA_FORMAT, "%d °",     DATA_INT,    wdir,
                "light_klx",        "Light",        DATA_FORMAT, "%.1f klx", DATA_DOUBLE, light_klx,
                //"mic",              "Integrity",    DATA_STRING, "CRC",
                NULL);
        /* clang-format on */

        decoder_output_data(decoder, data);
        return 1;
    }

    int chk      = (msg[0] << 8) | (msg[1]);
    uint32_t id  = ((uint32_t)msg[2] << 24) | (msg[3] << 16) | (msg[4] << 8) | (msg[5]);
    int flags    = (msg[6] >> 4);
    int batt     = (msg[6] >> 3) & 1;
    int chan     = (msg[6] & 0x7);
    int temp_raw = (msg[12] >> 4) * 100 + (msg[12] & 0x0f) * 10 + (msg[13] >> 4);
    //if (msg[25] & 0x0f)
    //    temp_raw = -temp_raw;
    float temp_c = temp_raw * 0.1f;
    int humidity = (msg[14] >> 4) * 10 + (msg[14] & 0x0f);

    /* clang-format off */
    data = data_make(
            "model",            "",             DATA_STRING, "Bresser-7in1indoor",
            "id",               "",             DATA_INT,    id,
            "channel",          "",             DATA_INT,    chan,
            "battery_ok",       "Battery",      DATA_INT,    !batt,
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temp_c,
            "humidity",         "Humidity",     DATA_INT,    humidity,
            "flags",            "Flags",        DATA_INT,    flags,
            //"mic",              "Integrity",    DATA_STRING, "CRC",
            NULL);
    /* clang-format on */

    decoder_output_data(decoder, data);
    return 1;
}

/**
Decoder for Bresser Weather Center 5-in-1.

The compact 5-in-1 multifunction outdoor sensor transmits the data
on 868.3 MHz.
The device uses FSK-PCM encoding,
The device sends a transmission every 12 seconds.
A transmission starts with a preamble of 0xAA.

Decoding borrowed from https://github.com/andreafabrizi/BresserWeatherCenter

Preamble:
aa aa aa aa aa 2d d4

Packet payload without preamble (203 bits):
0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25
-----------------------------------------------------------------------------
ee 93 7f f7 bf fb ef 9e fe ae bf ff ff 11 6c 80 08 40 04 10 61 01 51 40 00 00
ed 93 7f ff 0f ff ef b8 fe 7d bf ff ff 12 6c 80 00 f0 00 10 47 01 82 40 00 00
eb 93 7f eb 9f ee ef fc fc d6 bf ff ff 14 6c 80 14 60 11 10 03 03 29 40 00 00
ed 93 7f f7 cf f7 ef ed fc ce bf ff ff 12 6c 80 08 30 08 10 12 03 31 40 00 00
f1 fd 7f ff af ff ef bd fd b7 c9 ff ff 0e 02 80 00 50 00 10 42 02 48 36 00 00 00 00 (from https://github.com/merbanan/rtl_433/issues/719#issuecomment-388896758)
ee b7 7f ff 1f ff ef cb fe 7b d7 fc ff 11 48 80 00 e0 00 10 34 01 84 28 03 00 (from https://github.com/andreafabrizi/BresserWeatherCenter)
e3 fd 7f 89 7e 8a ed 68 fe af 9b fd ff 1c 02 80 76 81 75 12 97 01 50 64 02 00 00 00 (Large Wind Values, Gust=37.4m/s Avg=27.5m/s from https://github.com/merbanan/rtl_433/issues/1315)
ef a1 ff ff 1f ff ef dc ff de df ff 7f 10 5e 00 00 e0 00 10 23 00 21 20 00 80 00 00 (low batt +ve temp)
ed a1 ff ff 1f ff ef 8f ff d6 df ff 77 12 5e 00 00 e0 00 10 70 00 29 20 00 88 00 00 (low batt -ve temp -7.0C)
ec 91 ff ff 1f fb ef e7 fe ad ed ff f7 13 6e 00 00 e0 04 10 18 01 52 12 00 08 00 00 (good batt -ve temp)
CC CC CC CC CC CC CC CC CC CC CC CC CC uu II    GG DG WW  W TT  T HH RR  R Bt
                                              G-MSB ^     ^ W-MSB  (strange but consistent order)
C = Check, inverted data of 13 byte further
uu = checksum (number/count of set bits within bytes 14-25)
I = station ID (maybe)
G = wind gust in 1/10 m/s, normal binary coded, GGxG = 0x76D1 => 0x0176 = 256 + 118 = 374 => 37.4 m/s.  MSB is out of sequence.
D = wind direction 0..F = N..NNE..E..S..W..NNW
W = wind speed in 1/10 m/s, BCD coded, WWxW = 0x7512 => 0x0275 = 275 => 27.5 m/s. MSB is out of sequence.
T = temperature in 1/10 °C, BCD coded, TTxT = 1203 => 31.2 °C
t = temperature sign, minus if unequal 0
H = humidity in percent, BCD coded, HH = 23 => 23 %
R = rain in mm, BCD coded, RRxR = 1203 => 31.2 mm
B = Battery. 0=Ok, 8=Low.

*/

#include "decoder.h"


static int bresser_5in1_callback(r_device *decoder, bitbuffer_t *bitbuffer)
{
    uint8_t const preamble_pattern[] = { 0xaa, 0xaa, 0xaa, 0x2d, 0xd4 };

    data_t *data;
    uint8_t msg[26];
    uint16_t sensor_id;
    unsigned len = 0;

    // piggy-back for now
    int ret = bresser_7in1_callback(decoder, bitbuffer);
    if (ret > 0)
        return ret;

    if (bitbuffer->num_rows != 1
            || bitbuffer->bits_per_row[0] < 248
            || bitbuffer->bits_per_row[0] > 440) {
        if (decoder->verbose > 1) {
            fprintf(stderr, "%s bit_per_row %u out of range\n", __func__, bitbuffer->bits_per_row[0]);
        }
        return DECODE_ABORT_LENGTH; // Unrecognized data
    }

    unsigned start_pos = bitbuffer_search(bitbuffer, 0, 0,
            preamble_pattern, sizeof (preamble_pattern) * 8);

    if (start_pos == bitbuffer->bits_per_row[0]) {
        return DECODE_FAIL_SANITY;
    }
    start_pos += sizeof (preamble_pattern) * 8;
    len = bitbuffer->bits_per_row[0] - start_pos;
    if (((len + 7) / 8) < sizeof (msg)) {
        if (decoder->verbose > 1) {
            fprintf(stderr, "%s %u too short\n", __func__, len);
        }
        return DECODE_ABORT_LENGTH; // message too short
    }
    // truncate any excessive bits
    len = MIN(len, sizeof (msg) * 8);

    bitbuffer_extract_bytes(bitbuffer, 0, start_pos, msg, len);

    // First 13 bytes need to match inverse of last 13 bytes
    for (unsigned col = 0; col < sizeof (msg) / 2; ++col) {
        if ((msg[col] ^ msg[col + 13]) != 0xff) {
            if (decoder->verbose > 1) {
                fprintf(stderr, "%s Parity wrong at %u\n", __func__, col);
            }
            return DECODE_FAIL_MIC; // message isn't correct
        }
    }

    sensor_id = msg[14];

    int temp_raw = (msg[20] & 0x0f) + ((msg[20] & 0xf0) >> 4) * 10 + (msg[21] &0x0f) * 100;
    if (msg[25] & 0x0f)
        temp_raw = -temp_raw;
    float temperature = temp_raw * 0.1f;

    int humidity = (msg[22] & 0x0f) + ((msg[22] & 0xf0) >> 4) * 10;

    float wind_direction_deg = ((msg[17] & 0xf0) >> 4) * 22.5f;

    int gust_raw = ((msg[17] & 0x0f) << 8) + msg[16]; //fix merbanan/rtl_433#1315
    float wind_gust = gust_raw * 0.1f;

    int wind_raw = (msg[18] & 0x0f) + ((msg[18] & 0xf0) >> 4) * 10 + (msg[19] & 0x0f) * 100; //fix merbanan/rtl_433#1315
    float wind_avg = wind_raw * 0.1f;

    int rain_raw = (msg[23] & 0x0f) + ((msg[23] & 0xf0) >> 4) * 10 + (msg[24] & 0x0f) * 100;
    float rain = rain_raw * 0.1f;

    int battery_ok = ((msg[25] & 0x80) == 0);

    data = data_make(
            "model",            "",             DATA_STRING, "Bresser-5in1",
            "id",               "",             DATA_INT,    sensor_id,
            "battery",          "Battery",      DATA_STRING, battery_ok ? "OK": "LOW",
            "temperature_C",    "Temperature",  DATA_FORMAT, "%.1f C", DATA_DOUBLE, temperature,
            "humidity",         "Humidity",     DATA_INT, humidity,
            _X("wind_max_m_s","wind_gust"),        "Wind Gust",    DATA_FORMAT, "%.1f m/s",DATA_DOUBLE, wind_gust,
            _X("wind_avg_m_s","wind_speed"),       "Wind Speed",   DATA_FORMAT, "%.1f m/s",DATA_DOUBLE, wind_avg,
            "wind_dir_deg",     "Direction",    DATA_FORMAT, "%.1f °",DATA_DOUBLE, wind_direction_deg,
            "rain_mm",          "Rain",         DATA_FORMAT, "%.1f mm",DATA_DOUBLE, rain,
            "mic",              "Integrity",    DATA_STRING, "CHECKSUM",
            NULL);

    decoder_output_data(decoder, data);

    return 1;
}

static char *output_fields[] = {
    "model",
    "id",
    "battery",
    "temperature_C",
    "humidity",
    "wind_gust", // TODO: delete this
    "wind_speed", // TODO: delete this
    "wind_max_m_s",
    "wind_avg_m_s",
    "wind_dir_deg",
    "rain_mm",
    "mic",
    NULL,
};

r_device bresser_5in1 = {
    .name          = "Bresser Weather Center 5-in-1",
    .modulation    = FSK_PULSE_PCM,
    .short_width   = 124,
    .long_width    = 124,
    .reset_limit   = 25000,
    .decode_fn     = &bresser_5in1_callback,
    .disabled      = 0,
    .fields        = output_fields,
};
