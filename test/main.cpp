// Copyright (c) Team CharLS.
// SPDX-License-Identifier: BSD-3-Clause

#include "../src/default_traits.h"
#include "../src/lossless_traits.h"
#include "../src/process_line.h"

#include "util.h"

#include "bitstreamdamage.h"
#include "compliance.h"
#include "dicomsamples.h"
#include "legacy.h"
#include "performance.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using std::array;
using std::basic_filebuf;
using std::cout;
using std::error_code;
using std::fstream;
using std::getline;
using std::ios;
using std::ios_base;
using std::istream;
using std::iter_swap;
using std::mt19937;
using std::ostream;
using std::streamoff;
using std::string;
using std::stringstream;
using std::uniform_int_distribution;
using std::vector;
using namespace charls;

namespace {

constexpr ios::openmode mode_input = ios::in | ios::binary;
constexpr ios::openmode mode_output = ios::out | ios::binary;


vector<uint8_t> scan_file(const char* name_encoded, JlsParameters* params)
{
    vector<uint8_t> buffer = read_file(name_encoded);

    basic_filebuf<char> jls_file;
    jls_file.open(name_encoded, mode_input);

    const byte_stream_info raw_stream_info{&jls_file, nullptr, 0};

    const error_code error = JpegLsReadHeaderStream(raw_stream_info, params);
    if (error)
        throw unit_test_exception();

    return buffer;
}


void test_traits16_bit()
{
    const auto traits1 = default_traits<uint16_t, uint16_t>(4095, 0);
    using lossless_traits = lossless_traits<uint16_t, 12>;

    assert::is_true(traits1.limit == lossless_traits::limit);
    assert::is_true(traits1.maximum_sample_value == lossless_traits::maximum_sample_value);
    assert::is_true(traits1.reset_threshold == lossless_traits::reset_threshold);
    assert::is_true(traits1.bits_per_pixel == lossless_traits::bits_per_pixel);
    assert::is_true(traits1.quantized_bits_per_pixel == lossless_traits::quantized_bits_per_pixel);

    for (int i = -4096; i < 4096; ++i)
    {
        assert::is_true(traits1.modulo_range(i) == lossless_traits::modulo_range(i));
        assert::is_true(traits1.compute_error_value(i) == lossless_traits::compute_error_value(i));
    }

    for (int i = -8095; i < 8095; ++i)
    {
        assert::is_true(traits1.correct_prediction(i) == lossless_traits::correct_prediction(i));
        assert::is_true(traits1.is_near(i, 2) == lossless_traits::is_near(i, 2));
    }
}


void test_traits8_bit()
{
    const auto traits1 = default_traits<uint8_t, uint8_t>(255, 0);
    using lossless_traits = lossless_traits<uint8_t, 8>;

    assert::is_true(traits1.limit == lossless_traits::limit);
    assert::is_true(traits1.maximum_sample_value == lossless_traits::maximum_sample_value);
    assert::is_true(traits1.reset_threshold == lossless_traits::reset_threshold);
    assert::is_true(traits1.bits_per_pixel == lossless_traits::bits_per_pixel);
    assert::is_true(traits1.quantized_bits_per_pixel == lossless_traits::quantized_bits_per_pixel);

    for (int i = -255; i < 255; ++i)
    {
        assert::is_true(traits1.modulo_range(i) == lossless_traits::modulo_range(i));
        assert::is_true(traits1.compute_error_value(i) == lossless_traits::compute_error_value(i));
    }

    for (int i = -255; i < 512; ++i)
    {
        assert::is_true(traits1.correct_prediction(i) == lossless_traits::correct_prediction(i));
        assert::is_true(traits1.is_near(i, 2) == lossless_traits::is_near(i, 2));
    }
}


vector<uint8_t> make_some_noise(const size_t length, const size_t bit_count, const int seed)
{
    const auto max_value = (1U << bit_count) - 1U;
    mt19937 generator(seed);
    MSVC_CONST uniform_int_distribution<uint32_t> distribution(0, max_value);

    vector<uint8_t> buffer(length);
    for (auto& pixel_value : buffer)
    {
        pixel_value = static_cast<uint8_t>(distribution(generator));
    }

    return buffer;
}


vector<uint8_t> make_some_noise16_bit(const size_t length, const int bit_count, const int seed)
{
    const auto max_value = static_cast<uint16_t>((1U << bit_count) - 1U);
    mt19937 generator(seed);
    MSVC_CONST uniform_int_distribution<uint16_t> distribution(0, max_value);

    vector<uint8_t> buffer(length * 2);
    for (size_t i = 0; i < length; i = i + 2)
    {
        const uint16_t value = distribution(generator);

        buffer[i] = static_cast<uint8_t>(value);
        buffer[i] = static_cast<uint8_t>(value >> 8);
    }

    return buffer;
}


void test_noise_image()
{
    const rect_size size2 = rect_size(512, 512);

    for (size_t bit_depth = 8; bit_depth >= 2; --bit_depth)
    {
        stringstream label;
        label << "noise, bit depth: " << bit_depth;

        const vector<uint8_t> noise_bytes = make_some_noise(size2.cx * size2.cy, bit_depth, 21344);
        test_round_trip(label.str().c_str(), noise_bytes, size2, static_cast<int>(bit_depth), 1);
    }

    for (int bit_depth = 16; bit_depth > 8; --bit_depth)
    {
        stringstream label;
        label << "noise, bit depth: " << bit_depth;

        const vector<uint8_t> noise_bytes = make_some_noise16_bit(size2.cx * size2.cy, bit_depth, 21344);
        test_round_trip(label.str().c_str(), noise_bytes, size2, bit_depth, 1);
    }
}


void test_noise_image_with_custom_reset()
{
    const rect_size size{512, 512};
    constexpr int bit_depth{16};
    const vector<uint8_t> noise_bytes = make_some_noise16_bit(size.cx * size.cy, bit_depth, 21344);

    JlsParameters params{};
    params.components = 1;
    params.bitsPerSample = bit_depth;
    params.height = static_cast<int>(size.cy);
    params.width = static_cast<int>(size.cx);
    params.custom.MaximumSampleValue = (1 << bit_depth) - 1;
    params.custom.ResetValue = 63;

    test_round_trip("TestNoiseImageWithCustomReset", noise_bytes, params);
}


void test_fail_on_too_small_output_buffer()
{
    const auto input_buffer = make_some_noise(8 * 8, 8, 21344);

    // Trigger a "destination buffer too small" when writing the header markers.
    try
    {
        vector<uint8_t> output_buffer(1);
        jpegls_encoder encoder;
        encoder.destination(output_buffer);
        encoder.frame_info({8, 8, 8, 1});
        static_cast<void>(encoder.encode(input_buffer));
        assert::is_true(false);
    }
    catch (const jpegls_error& e)
    {
        assert::is_true(e.code() == jpegls_errc::destination_buffer_too_small);
    }

    // Trigger a "destination buffer too small" when writing the encoded pixel bytes.
    try
    {
        vector<uint8_t> output_buffer(100);
        jpegls_encoder encoder;
        encoder.destination(output_buffer);
        encoder.frame_info({8, 8, 8, 1});
        static_cast<void>(encoder.encode(input_buffer));
        assert::is_true(false);
    }
    catch (const jpegls_error& e)
    {
        assert::is_true(e.code() == jpegls_errc::destination_buffer_too_small);
    }
}


void test_bgra()
{
    array<uint8_t, 20> input{'R', 'G', 'B', 'A', 'R', 'G', 'B', 'A', 'R', 'G', 'B', 'A', 'R', 'G', 'B', 'A', 1, 2, 3, 4};
    const array<uint8_t, 20> expected{'B', 'G', 'R', 'A', 'B', 'G', 'R', 'A', 'B', 'G', 'R', 'A', 'B', 'G', 'R', 'A', 1, 2, 3, 4};

    transform_rgb_to_bgr(input.data(), 4, 4);
    assert::is_true(expected == input);
}


void test_bgr()
{
    JlsParameters params{};
    vector<uint8_t> encoded_buffer = scan_file("test/conformance/T8C2E3.JLS", &params);
    vector<uint8_t> decoded_buffer(static_cast<size_t>(params.width) * params.height * params.components);

    params.outputBgr = static_cast<char>(true);

    // ReSharper disable CppDeprecatedEntity
    DISABLE_DEPRECATED_WARNING

    const error_code error = JpegLsDecode(decoded_buffer.data(), decoded_buffer.size(), encoded_buffer.data(), encoded_buffer.size(), &params, nullptr);

    // ReSharper restore CppDeprecatedEntity
    RESTORE_DEPRECATED_WARNING

    assert::is_true(!error);

    assert::is_true(decoded_buffer[0] == 0x69);
    assert::is_true(decoded_buffer[1] == 0x77);
    assert::is_true(decoded_buffer[2] == 0xa1);
    assert::is_true(decoded_buffer[static_cast<size_t>(params.width) * 6 + 3] == 0x2d);
    assert::is_true(decoded_buffer[static_cast<size_t>(params.width) * 6 + 4] == 0x43);
    assert::is_true(decoded_buffer[static_cast<size_t>(params.width) * 6 + 5] == 0x4d);
}


void test_too_small_output_buffer()
{
    const vector<uint8_t> encoded = read_file("test/lena8b.jls");
    vector<uint8_t> destination(512 * 511);

    jpegls_decoder decoder;
    decoder.source(encoded).read_header();

    error_code error;
    try
    {
        decoder.decode(destination);
    }
    catch (const jpegls_error& e)
    {
        error = e.code();
    }

    assert::is_true(error == jpegls_errc::destination_buffer_too_small);
}


////void TestBadImage()
////{
////    vector<uint8_t> rgbyteCompressed;
////    if (!ReadFile("test/BadCompressedStream.jls", &rgbyteCompressed, 0))
////        return;
////
////    vector<uint8_t> rgbyteOut(2500 * 3000 * 2);
////    auto error = JpegLsDecode(&rgbyteOut[0], rgbyteOut.size(), &rgbyteCompressed[0], rgbyteCompressed.size(), nullptr, nullptr);
////
////    Assert::IsTrue(error == jpegls_errc::UncompressedBufferTooSmall);
////}


void test_decode_bit_stream_with_no_marker_start()
{
    const array<uint8_t, 2> encoded_data{0x33, 0x33};
    array<uint8_t, 1000> output{};

    error_code error;
    try
    {
        jpegls_decoder decoder;
        decoder.source(encoded_data).read_header();
        decoder.decode(output);
    }
    catch (const jpegls_error& e)
    {
        error = e.code();
    }

    assert::is_true(error == jpegls_errc::jpeg_marker_start_byte_not_found);
}


void test_decode_bit_stream_with_unsupported_encoding()
{
    const array<uint8_t, 6> encoded_data{
        0xFF, 0xD8, // Start Of Image (JPEG_SOI)
        0xFF, 0xC3, // Start Of Frame (lossless, Huffman) (JPEG_SOF_3)
        0x00, 0x00  // Length of data of the marker
    };
    array<uint8_t, 1000> output{};

    error_code error;
    try
    {
        jpegls_decoder decoder;
        decoder.source(encoded_data).read_header();
        decoder.decode(output);
    }
    catch (const jpegls_error& e)
    {
        error = e.code();
    }

    assert::is_true(error == jpegls_errc::encoding_not_supported);
}


void test_decode_bit_stream_with_unknown_jpeg_marker()
{
    const array<uint8_t, 6> encoded_data{
        0xFF, 0xD8, // Start Of Image (JPEG_SOI)
        0xFF, 0x01, // Undefined marker
        0x00, 0x00  // Length of data of the marker
    };
    array<uint8_t, 1000> output{};

    error_code error;
    try
    {
        jpegls_decoder decoder;
        decoder.source(encoded_data).read_header();
        decoder.decode(output);
    }
    catch (const jpegls_error& e)
    {
        error = e.code();
    }

    assert::is_true(error == jpegls_errc::unknown_jpeg_marker_found);
}


void test_decode_rect()
{
    JlsParameters params{};
    vector<uint8_t> encoded_data = scan_file("test/lena8b.jls", &params);
    vector<uint8_t> decoded_buffer(static_cast<size_t>(params.width) * params.height * params.components);

    // ReSharper disable CppDeprecatedEntity
    DISABLE_DEPRECATED_WARNING

    error_code error = JpegLsDecode(decoded_buffer.data(), decoded_buffer.size(), encoded_data.data(), encoded_data.size(), nullptr, nullptr);
    assert::is_true(!error);

    const JlsRect rect = {128, 128, 256, 1};
    vector<uint8_t> decoded_data(static_cast<size_t>(rect.Width) * rect.Height);
    decoded_data.push_back(0x1f);

    error = JpegLsDecodeRect(decoded_data.data(), decoded_data.size(), encoded_data.data(), encoded_data.size(), rect, nullptr, nullptr);

    // ReSharper restore CppDeprecatedEntity
    RESTORE_DEPRECATED_WARNING

    assert::is_true(!error);

    assert::is_true(memcmp(&decoded_buffer[rect.X + static_cast<size_t>(rect.Y) * 512], decoded_data.data(), static_cast<size_t>(rect.Width) * rect.Height) == 0);
    assert::is_true(decoded_data[static_cast<size_t>(rect.Width) * rect.Height] == 0x1f);
}


void test_encode_from_stream(const char* file, const int offset, const int width, const int height, const int bpp, const int component_count, const interleave_mode ilv, const size_t expected_length)
{
    basic_filebuf<char> my_file; // On the stack
    my_file.open(file, mode_input);
    assert::is_true(my_file.is_open());

    my_file.pubseekoff(static_cast<streamoff>(offset), ios_base::cur);
    const byte_stream_info raw_stream_info = {&my_file, nullptr, 0};

    vector<uint8_t> compressed(static_cast<size_t>(width) * height * component_count * 2);
    JlsParameters params = JlsParameters();
    params.height = height;
    params.width = width;
    params.components = component_count;
    params.bitsPerSample = bpp;
    params.interleaveMode = ilv;
    size_t bytes_written{};

    JpegLsEncodeStream(from_byte_array(compressed.data(), static_cast<size_t>(width) * height * component_count * 2), bytes_written, raw_stream_info, params);
    assert::is_true(bytes_written == expected_length);

    my_file.close();
}


bool decode_to_pnm(istream& input, ostream& output)
{
    const byte_stream_info input_info{input.rdbuf(), nullptr, 0};

    auto params = JlsParameters();
    error_code error = JpegLsReadHeaderStream(input_info, &params);
    if (error)
        return false;

    input.seekg(0);

    const int max_value = (1 << params.bitsPerSample) - 1;
    const int bytes_per_sample = max_value > 255 ? 2 : 1;
    vector<uint8_t> output_buffer(static_cast<size_t>(params.width) * params.height * bytes_per_sample * params.components);
    const auto output_info = from_byte_array(output_buffer.data(), output_buffer.size());
    error = JpegLsDecodeStream(output_info, input_info, &params);
    if (error)
        return false;

    // PNM format requires most significant byte first (big endian).
    if (bytes_per_sample == 2)
    {
        for (auto i = output_buffer.begin(); i != output_buffer.end(); i += 2)
        {
            iter_swap(i, i + 1);
        }
    }

    const int magic_number = params.components == 3 ? 6 : 5;
    output << 'P' << magic_number << "\n"
           << params.width << ' ' << params.height << "\n"
           << max_value << "\n";
    output.write(reinterpret_cast<char*>(output_buffer.data()), output_buffer.size());

    return true;
}


vector<int> read_pnm_header(istream& pnm_file)
{
    vector<int> read_values;

    const auto first = static_cast<char>(pnm_file.get());

    // All portable anymap format (PNM) start with the character P.
    if (first != 'P')
        return read_values;

    while (read_values.size() < 4)
    {
        string bytes;
        getline(pnm_file, bytes);
        stringstream line(bytes);

        while (read_values.size() < 4)
        {
            int value = -1;
            line >> value;
            if (value <= 0)
                break;

            read_values.push_back(value);
        }
    }
    return read_values;
}


// Purpose: this function can encode an image stored in the Portable Anymap Format (PNM)
//          into the JPEG-LS format. The 2 binary formats P5 and P6 are supported:
//          Portable GrayMap: P5 = binary, extension = .pgm, 0-2^16 (gray scale)
//          Portable PixMap: P6 = binary, extension.ppm, range 0-2^16 (RGB)
bool encode_pnm(istream& pnm_file, const ostream& jls_file_stream)
{
    vector<int> read_values = read_pnm_header(pnm_file);
    if (read_values.size() != 4)
        return false;

    JlsParameters params{};
    params.components = read_values[0] == 6 ? 3 : 1;
    params.width = read_values[1];
    params.height = read_values[2];
    params.bitsPerSample = log_2(read_values[3] + 1);
    params.interleaveMode = params.components == 3 ? interleave_mode::line : interleave_mode::none;

    const int bytes_per_sample = ::bit_to_byte_count(params.bitsPerSample);
    vector<uint8_t> input_buffer(static_cast<size_t>(params.width) * params.height * bytes_per_sample * params.components);
    pnm_file.read(reinterpret_cast<char*>(input_buffer.data()), input_buffer.size());
    if (!pnm_file.good())
        return false;

    // PNM format is stored with most significant byte first (big endian).
    if (bytes_per_sample == 2)
    {
        for (auto i = input_buffer.begin(); i != input_buffer.end(); i += 2)
        {
            iter_swap(i, i + 1);
        }
    }

    const auto raw_stream_info = from_byte_array(input_buffer.data(), input_buffer.size());
    const byte_stream_info jls_stream_info = {jls_file_stream.rdbuf(), nullptr, 0};

    size_t bytes_written{};
    JpegLsEncodeStream(jls_stream_info, bytes_written, raw_stream_info, params);
    return true;
}


bool compare_pnm(istream& pnm_file1, istream& pnm_file2)
{
    vector<int> header1 = read_pnm_header(pnm_file1);
    if (header1.size() != 4)
    {
        cout << "Cannot read header from input file 1\n";
        return false;
    }

    vector<int> header2 = read_pnm_header(pnm_file2);
    if (header2.size() != 4)
    {
        cout << "Cannot read header from input file 2\n";
        return false;
    }

    if (header1[0] != header2[0])
    {
        cout << "Header type " << header1[0] << " is not equal with type " << header2[0] << "\n";
        return false;
    }

    const size_t width = header1[1];
    if (width != static_cast<size_t>(header2[1]))
    {
        cout << "Width " << width << " is not equal with width " << header2[1] << "\n";
        return false;
    }

    const size_t height = header1[2];
    if (height != static_cast<size_t>(header2[2]))
    {
        cout << "Height " << height << " is not equal with height " << header2[2] << "\n";
        return false;
    }

    if (header1[3] != header2[3])
    {
        cout << "max-value " << header1[3] << " is not equal with max-value " << header2[3] << "\n";
        return false;
    }
    const auto bytes_per_sample = header1[3] > 255 ? 2 : 1;

    const size_t byte_count = width * height * bytes_per_sample;
    vector<uint8_t> bytes1(byte_count);
    vector<uint8_t> bytes2(byte_count);

    pnm_file1.read(reinterpret_cast<char*>(&bytes1[0]), byte_count);
    pnm_file2.read(reinterpret_cast<char*>(&bytes2[0]), byte_count);

    for (size_t x = 0; x < height; ++x)
    {
        for (size_t y = 0; y < width; y += bytes_per_sample)
        {
            if (bytes_per_sample == 1)
            {
                if (bytes1[(x * width) + y] != bytes2[(x * width) + y])
                {
                    cout << "Values of the 2 files are different, height:" << x << ", width:" << y << "\n";
                    return false;
                }
            }
            else
            {
                if (bytes1[(x * width) + y] != bytes2[(x * width) + y] ||
                    bytes1[(x * width) + (y + 1)] != bytes2[(x * width) + (y + 1)])
                {
                    cout << "Values of the 2 files are different, height:" << x << ", width:" << y << "\n";
                    return false;
                }
            }
        }
    }

    cout << "Values of the 2 files are equal\n";
    return true;
}


////void TestDecodeFromStream(const char* name_encoded)
////{
////    basic_filebuf<char> jlsFile;
////    jlsFile.open(name_encoded, mode_input);
////    Assert::IsTrue(jlsFile.is_open());
////    ByteStreamInfo compressedByteStream = {&jlsFile, nullptr, 0};
////
////    auto params = JlsParameters();
////    auto err = JpegLsReadHeaderStream(compressedByteStream, &params, nullptr);
////    Assert::IsTrue(err == jpegls_errc::OK);
////
////    jlsFile.pubseekpos(ios::beg, ios_base::in);
////
////    basic_stringbuf<char> buf;
////    ByteStreamInfo rawStreamInfo = {&buf, nullptr, 0};
////
////    err = JpegLsDecodeStream(rawStreamInfo, compressedByteStream, nullptr, nullptr);
////    ////size_t outputCount = buf.str().size();
////
////    Assert::IsTrue(err == jpegls_errc::OK);
////    //Assert::IsTrue(outputCount == 512 * 512);
////}


jpegls_errc decode_raw(const char* name_encoded, const char* name_output)
{
    fstream jls_file(name_encoded, mode_input);
    const byte_stream_info compressed_byte_stream{jls_file.rdbuf(), nullptr, 0};

    fstream raw_file(name_output, mode_output);
    const byte_stream_info raw_stream{raw_file.rdbuf(), nullptr, 0};

    const auto value = JpegLsDecodeStream(raw_stream, compressed_byte_stream, nullptr);
    jls_file.close();
    raw_file.close();

    return value;
}


void test_encode_from_stream()
{
    ////test_encode_from_stream("test/user_supplied/output.jls");

    test_encode_from_stream("test/0015.raw", 0, 1024, 1024, 8, 1, interleave_mode::none, 0x3D3ee);
    ////test_encode_from_stream("test/MR2_UNC", 1728, 1024, 1024, 16, 1,0, 0x926e1);
    test_encode_from_stream("test/conformance/TEST8.PPM", 15, 256, 256, 8, 3, interleave_mode::sample, 99734);
    test_encode_from_stream("test/conformance/TEST8.PPM", 15, 256, 256, 8, 3, interleave_mode::line, 100615);
}


void unit_test()
{
    try
    {
        //// TestBadImage();

        cout << "Test Conformance\n";
        test_encode_from_stream();
        test_conformance();

        test_decode_rect();

        cout << "Test Traits\n";
        test_traits16_bit();
        test_traits8_bit();

        cout << "Windows bitmap BGR/BGRA output\n";
        test_bgr();
        test_bgra();

        cout << "Test Small buffer\n";
        test_too_small_output_buffer();

        test_fail_on_too_small_output_buffer();

        cout << "Test Color transform equivalence on HP images\n";
        test_color_transforms_hp_images();

        cout << "Test Annex H3\n";
        test_sample_annex_h3();

        test_noise_image();
        test_noise_image_with_custom_reset();

        cout << "Test robustness\n";
        test_decode_bit_stream_with_no_marker_start();
        test_decode_bit_stream_with_unsupported_encoding();
        test_decode_bit_stream_with_unknown_jpeg_marker();

        cout << "Test Legacy API\n";
        test_legacy_api();
    }
    catch (const unit_test_exception&)
    {
        cout << "==> Unit test failed <==\n";
    }
}

} // namespace


int main(const int argc, const char* const argv[]) // NOLINT(bugprone-exception-escape)
{
    if (argc == 1)
    {
        cout << "CharLS test runner.\nOptions: -unittest, -bitstreamdamage, -performance[:loop-count], -decodeperformance[:loop-count], -decoderaw -encodepnm -decodetopnm -comparepnm -legacy\n";
        return EXIT_FAILURE;
    }

    for (int i = 1; i < argc; ++i)
    {
        string str = argv[i];
        if (str == "-unittest")
        {
            unit_test();
            continue;
        }

        if (str == "-decoderaw")
        {
            if (i != 1 || argc != 4)
            {
                cout << "Syntax: -decoderaw inputfile outputfile\n";
                return EXIT_FAILURE;
            }
            return make_error_code(decode_raw(argv[2], argv[3])) ? EXIT_FAILURE : EXIT_SUCCESS;
        }

        if (str == "-decodetopnm")
        {
            if (i != 1 || argc != 4)
            {
                cout << "Syntax: -decodetopnm inputfile outputfile\n";
                return EXIT_FAILURE;
            }
            fstream pnm_file(argv[3], mode_output);
            fstream jls_file(argv[2], mode_input);

            return decode_to_pnm(jls_file, pnm_file) ? EXIT_SUCCESS : EXIT_FAILURE;
        }

        if (str == "-encodepnm")
        {
            if (i != 1 || argc != 4)
            {
                cout << "Syntax: -encodepnm inputfile outputfile\n";
                return EXIT_FAILURE;
            }
            fstream pnm_file(argv[2], mode_input);
            const fstream jls_file(argv[3], mode_output);

            return encode_pnm(pnm_file, jls_file) ? EXIT_SUCCESS : EXIT_FAILURE;
        }

        if (str == "-comparepnm")
        {
            if (i != 1 || argc != 4)
            {
                cout << "Syntax: -encodepnm inputfile outputfile\n";
                return EXIT_FAILURE;
            }
            fstream pnm_file1(argv[2], mode_input);
            fstream pnm_file2(argv[3], mode_input);

            return compare_pnm(pnm_file1, pnm_file2) ? EXIT_SUCCESS : EXIT_FAILURE;
        }

        if (str == "-bitstreamdamage")
        {
            damaged_bit_stream_tests();
            continue;
        }

        if (str.compare(0, 12, "-performance") == 0)
        {
            int loop_count{1};

            // Extract the optional loop count from the command line. Longer running tests make the measurements more reliable.
            auto index = str.find(':');
            if (index != string::npos)
            {
                loop_count = stoi(str.substr(++index));
                if (loop_count < 1)
                {
                    cout << "Loop count not understood or invalid: %s" << str << "\n";
                    break;
                }
            }

            performance_tests(loop_count);
            continue;
        }

        if (str.compare(0, 17, "-rgb8_performance") == 0)
        {
            // See the comments in function, how to prepare this test.
            test_large_image_performance_rgb8(1);
            continue;
        }

        if (str.compare(0, 18, "-decodeperformance") == 0)
        {
            int loop_count{1};

            // Extract the optional loop count from the command line. Longer running tests make the measurements more reliable.
            auto index = str.find(':');
            if (index != string::npos)
            {
                loop_count = stoi(str.substr(++index));
                if (loop_count < 1)
                {
                    cout << "Loop count not understood or invalid: " << str << "\n";
                    break;
                }
            }

            decode_performance_tests(loop_count);
            continue;
        }

        if (str == "-dicom")
        {
            test_dicom_wg4_images();
            continue;
        }

        if (str == "-legacy")
        {
            test_legacy_api();
            continue;
        }

        cout << "Option not understood: " << argv[i] << "\n";
        break;
    }

    return EXIT_SUCCESS;
}
