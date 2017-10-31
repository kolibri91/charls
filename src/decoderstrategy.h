//
// (C) Jan de Vaan 2007-2010, all rights reserved. See the accompanying "License.txt" for licensed use.
//

#ifndef CHARLS_DECODERSTATEGY
#define CHARLS_DECODERSTATEGY


#include "util.h"
#include "processline.h"
#include "jpegmarkercode.h"
#include "codecbase.h"
#include <memory>

namespace charls
{

// Purpose: Implements encoding to stream of bits. In encoding mode JpegLsCodec inherits from EncoderStrategy
class DecoderStrategy : public CodecBase
{
public:
    explicit DecoderStrategy(const JlsParameters& params) :
        CodecBase(params)
    {
    }

    virtual ~DecoderStrategy() = default;

    virtual std::unique_ptr<ProcessLine> CreateProcess(ByteStreamInfo rawStreamInfo) = 0;

    virtual void DoScan() = 0;

    void DecodeScan(std::unique_ptr<ProcessLine> processLine, const JlsRect& rect, ByteStreamInfo& compressedData)
    {
        _processLine = std::move(processLine);

        uint8_t* compressedBytes = const_cast<uint8_t*>(static_cast<const uint8_t*>(compressedData.rawData));
        _rect = rect;

        Init(compressedData);
        DoScan();
        SkipBytes(compressedData, GetCurBytePos() - compressedBytes);
    }

    void Init(ByteStreamInfo& compressedStream)
    {
        _validBits = 0;
        _readCache = 0;

        if (compressedStream.rawStream)
        {
            _buffer.resize(40000);
            _position = _buffer.data();
            _endPosition = _position;
            _byteStream = compressedStream.rawStream;
            AddBytesFromStream();
        }
        else
        {
            _byteStream = nullptr;
            _position = compressedStream.rawData;
            _endPosition = _position + compressedStream.count;
        }

        _nextFFPosition = FindNextFF();
        MakeValid();
    }

    void AddBytesFromStream()
    {
        if (!_byteStream || _byteStream->sgetc() == std::char_traits<char>::eof())
            return;

        const std::size_t count = _endPosition - _position;

        if (count > 64)
            return;

        for (std::size_t i = 0; i < count; ++i)
        {
            _buffer[i] = _position[i];
        }
        const std::size_t offset = _buffer.data() - _position;

        _position += offset;
        _endPosition += offset;
        _nextFFPosition += offset;

        const std::streamsize readbytes = _byteStream->sgetn(reinterpret_cast<char*>(_endPosition), _buffer.size() - count);
        _endPosition += readbytes;
    }

    FORCE_INLINE void Skip(int32_t length) noexcept
    {
        _validBits -= length;
        _readCache = _readCache << length;
    }

    static void OnLineBegin(int32_t /*cpixel*/, void* /*ptypeBuffer*/, int32_t /*pixelStride*/) noexcept
    {
    }

    void OnLineEnd(int32_t pixelCount, const void* ptypeBuffer, int32_t pixelStride) const
    {
        _processLine->NewLineDecoded(ptypeBuffer, pixelCount, pixelStride);
    }

    void EndScan()
    {
        if (*_position != static_cast<uint8_t>(JpegMarkerCode::Start))
        {
            ReadBit();

            if (*_position != static_cast<uint8_t>(JpegMarkerCode::Start))
                throw charls_error(charls::ApiResult::TooMuchCompressedData);
        }

        if (_readCache != 0)
            throw charls_error(charls::ApiResult::TooMuchCompressedData);
    }

    FORCE_INLINE bool OptimizedRead() noexcept
    {
        // Easy & fast: if there is no 0xFF byte in sight, we can read without bit stuffing
        if (_position < _nextFFPosition - (sizeof(bufType)-1))
        {
            _readCache |= FromBigEndian<sizeof(bufType)>::Read(_position) >> _validBits;
            const int bytesToRead = (bufType_bit_count - _validBits) >> 3;
            _position += bytesToRead;
            _validBits += bytesToRead * 8;
            ASSERT(_validBits >= bufType_bit_count - 8);
            return true;
        }
        return false;
    }

    void MakeValid()
    {
        ASSERT(_validBits <=bufType_bit_count - 8);

        if (OptimizedRead())
            return;

        AddBytesFromStream();

        do
        {
            if (_position >= _endPosition)
            {
                if (_validBits <= 0)
                    throw charls_error(charls::ApiResult::InvalidCompressedData);

                return;
            }

            const bufType valnew = _position[0];

            if (valnew == static_cast<bufType>(JpegMarkerCode::Start))
            {
                // JPEG bit stream rule: no FF may be followed by 0x80 or higher
                if (_position == _endPosition - 1 || (_position[1] & 0x80) != 0)
                {
                    if (_validBits <= 0)
                        throw charls_error(charls::ApiResult::InvalidCompressedData);

                    return;
                }
            }

            _readCache |= valnew << (bufType_bit_count - 8 - _validBits);
            _position += 1;
            _validBits += 8;

            if (valnew == static_cast<bufType>(JpegMarkerCode::Start))
            {
                _validBits--;
            }
        }
        while (_validBits < bufType_bit_count - 8);

        _nextFFPosition = FindNextFF();
    }

    uint8_t* FindNextFF() const noexcept
    {
        auto positionNextFF = _position;

        while (positionNextFF < _endPosition)
        {
            if (*positionNextFF == static_cast<uint8_t>(JpegMarkerCode::Start))
                break;

            positionNextFF++;
        }

        return positionNextFF;
    }

    uint8_t* GetCurBytePos() const noexcept
    {
        int32_t validBits = _validBits;
        uint8_t* compressedBytes = _position;

        for (;;)
        {
            const int32_t cbitLast = compressedBytes[-1] == static_cast<uint8_t>(JpegMarkerCode::Start) ? 7 : 8;

            if (validBits < cbitLast)
                return compressedBytes;

            validBits -= cbitLast;
            compressedBytes--;
        }
    }

    FORCE_INLINE int32_t ReadValue(int32_t length)
    {
        if (_validBits < length)
        {
            MakeValid();
            if (_validBits < length)
                throw charls_error(charls::ApiResult::InvalidCompressedData);
        }

        ASSERT(length != 0 && length <= _validBits);
        ASSERT(length < 32);
        const int32_t result = static_cast<int32_t>(_readCache >> (bufType_bit_count - length));
        Skip(length);
        return result;
    }

    FORCE_INLINE int32_t PeekByte()
    {
        if (_validBits < 8)
        {
            MakeValid();
        }

        return static_cast<int32_t>(_readCache >> (bufType_bit_count - 8));
    }

    FORCE_INLINE bool ReadBit()
    {
        if (_validBits <= 0)
        {
            MakeValid();
        }

        const bool bSet = (_readCache & (bufType(1) << (bufType_bit_count - 1))) != 0;
        Skip(1);
        return bSet;
    }

    FORCE_INLINE int32_t Peek0Bits()
    {
        if (_validBits < 16)
        {
            MakeValid();
        }
        bufType valTest = _readCache;

        for (int32_t count = 0; count < 16; count++)
        {
            if ((valTest & (bufType(1) << (bufType_bit_count - 1))) != 0)
                return count;

            valTest <<= 1;
        }
        return -1;
    }

    FORCE_INLINE int32_t ReadHighbits()
    {
        const int32_t count = Peek0Bits();
        if (count >= 0)
        {
            Skip(count + 1);
            return count;
        }
        Skip(15);

        for (int32_t highbits = 15; ; highbits++)
        {
            if (ReadBit())
                return highbits;
        }
    }

    int32_t ReadLongValue(int32_t length)
    {
        if (length <= 24)
            return ReadValue(length);

        return (ReadValue(length - 24) << 24) + ReadValue(24);
    }

protected:
    std::unique_ptr<ProcessLine> _processLine;

private:
    using bufType = std::size_t;
    static constexpr size_t bufType_bit_count = sizeof(bufType) * 8;

    std::vector<uint8_t> _buffer;
    std::basic_streambuf<char>* _byteStream{};

    // decoding
    bufType _readCache{};
    int32_t _validBits{};
    uint8_t* _position{};
    uint8_t* _nextFFPosition{};
    uint8_t* _endPosition{};
};

}
#endif
