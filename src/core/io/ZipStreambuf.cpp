#include "ZipStreambuf.hpp"
#include "ZipEntry.hpp"

#include "math/MathUtil.hpp"

#include "Debug.hpp"

namespace Tungsten {

static const uint64 InputBufferSize = TINFL_LZ_DICT_SIZE;
static const uint64 OutputBufferSize = TINFL_LZ_DICT_SIZE;

ZipInputStreambuf::ZipInputStreambuf(InputStreamHandle in, mz_zip_archive &archive, const ZipEntry &entry)
: _in(std::move(in)),
  _inputStreamOffset(0),
  _outputStreamOffset(0),
  _inputAvail(0),
  _inputBufOffset(0),
  _outputBufOffset(0),
  _status(TINFL_STATUS_NEEDS_MORE_INPUT),
  _inputBuffer(new uint8[InputBufferSize]),
  _outputBuffer(new uint8[OutputBufferSize])
{
    if (!mz_zip_reader_parse_zip_file_header(&archive, entry.archiveIndex, &_header))
        FAIL("ZipInputStreambuf: Failed to parse Zip file header");

    _uncompressedSize  = _header.uncomp_size;
    _compressedSize    = _header.comp_size;

    tinfl_init(&_inflator);

    char *start = reinterpret_cast<char *>(_outputBuffer.get());
    setg(start, start, start);
}

ZipInputStreambuf::int_type ZipInputStreambuf::underflow()
{
    if (gptr() < egptr())
        return traits_type::to_int_type(*gptr());
    if (_status <= TINFL_STATUS_DONE)
        return traits_type::eof();

    _in->seekg(_header.file_ofs + _inputStreamOffset);
    do {
        size_t oldOutputBufOffset = _outputBufOffset;
        _outputStreamOffset += _outputBufOffset;
        _outputBufOffset = 0;

        do {
            if (_inputAvail == 0 && _in->good()) {
                _in->read(reinterpret_cast<char *>(_inputBuffer.get()),
                        min(InputBufferSize, _compressedSize - _inputStreamOffset));
                _inputAvail = _in->gcount();
                _inputStreamOffset += _inputAvail;
                _inputBufOffset = 0;
            }

            size_t inputBufSize = _inputAvail;
            size_t outputBufSize = OutputBufferSize - _outputBufOffset;
            _status = tinfl_decompress(
                &_inflator,
                _inputBuffer.get() + _inputBufOffset,
                &inputBufSize,
                _outputBuffer.get(),
                _outputBuffer.get() + _outputBufOffset,
                &outputBufSize,
                _inputAvail ? TINFL_FLAG_HAS_MORE_INPUT : 0
            );

            _inputAvail -= inputBufSize;
            _inputBufOffset += inputBufSize;
            _outputBufOffset += outputBufSize;
        } while (_status == TINFL_STATUS_NEEDS_MORE_INPUT);

        char *start = reinterpret_cast<char *>(_outputBuffer.get());
        setg(start, gptr() - oldOutputBufOffset, start + _outputBufOffset);

        if (_status <= TINFL_STATUS_DONE)
            break;
    } while (gptr() - eback() >= int64(_outputBufOffset));

    return traits_type::to_int_type(*gptr());
}

std::streampos ZipInputStreambuf::seekpos(std::streampos pos, std::ios_base::openmode /*which*/)
{
    if (pos >= int64(_outputStreamOffset)) {
        setg(eback(), eback() + (pos - int64(_outputStreamOffset)), egptr());
    } else {
        _inputStreamOffset = 0;
        _outputStreamOffset = 0;
        _inputAvail = 0;
        _inputBufOffset = 0;
        _outputBufOffset = 0;
        _status = TINFL_STATUS_NEEDS_MORE_INPUT;
        tinfl_init(&_inflator);

        char *start = reinterpret_cast<char *>(_outputBuffer.get());
        setg(start, start, start);
    }

    return pos;
}

std::streampos ZipInputStreambuf::seekoff(std::streamoff off, std::ios_base::seekdir way,
        std::ios_base::openmode which)
{
    if (way == std::ios_base::beg)
        return seekpos(off, which);
    else if (way == std::ios_base::cur)
        return seekpos(_outputStreamOffset + (gptr() - eback()) + off, which);
    else if (way == std::ios_base::end)
        return seekpos(_uncompressedSize + off, which);
    return -1;
}

}
