/******************************************************************************
 * @file SerialPort.cpp                                                       *
 * @copyright (C) 2004-2018 LibSerial Development Team. All rights reserved.  *
 * crayzeewulf@gmail.com                                                      *
 *                                                                            *
 * Redistribution and use in source and binary forms, with or without         *
 * modification, are permitted provided that the following conditions         *
 * are met:                                                                   *
 *                                                                            *
 * 1. Redistributions of source code must retain the above copyright          *
 *    notice, this list of conditions and the following disclaimer.           *
 * 2. Redistributions in binary form must reproduce the above copyright       *
 *    notice, this list of conditions and the following disclaimer in         *
 *    the documentation and/or other materials provided with the              *
 *    distribution.                                                           *
 * 3. Neither the name PX4 nor the names of its contributors may be           *
 *    used to endorse or promote products derived from this software          *
 *    without specific prior written permission.                              *
 *                                                                            *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS        *
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT          *
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS          *
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE             *
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,        *
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,       *
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS      *
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED         *
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT                *
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN          *
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE            *
 * POSSIBILITY OF SUCH DAMAGE.                                                *
 *****************************************************************************/
#include <emscripten/version.h>

static_assert((__EMSCRIPTEN_major__ * 100 * 100 + __EMSCRIPTEN_minor__ * 100 +
               __EMSCRIPTEN_tiny__) >= 30148,
              "Emscripten 3.1.48 or newer is required.");

#include "libserial/SerialPort.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sstream>
#include <sys/ioctl.h>
#include <type_traits>
#include <unistd.h>
#include <emscripten.h>
#include <emscripten/val.h>
#include <map>

using namespace emscripten;

#ifdef _REENTRANT
#include <emscripten/proxying.h>
#include <emscripten/threading.h>
#include <pthread.h>

static ProxyingQueue queue;
#endif

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-prototypes"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wshadow"

namespace {

    EM_ASYNC_JS(EM_VAL, start_callback, (EM_VAL port, LibSerial::SerialPort::Implementation* serialPort), {
        const abortController = new AbortController();

        const outputStream = new WritableStream({
            write: async (chunk) => {
                    // Chunk is a Uint8Array
                    std::vector<uint8_t> data;
                    for (let i = 0; i < chunk.length; i++) {
                        data.push(chunk[i]);
                    }
                    serialPort->mReadBuffer.insert(serialPort->mReadBuffer.end(), data.begin(), data.end());
            },
        });

        const pipePromise = port.readable.pipeTo(outputStream, { signal: abortController.signal });

        return abortController;
    });

    EM_ASYNC_JS(void, stop_callback, (EM_VAL abortController), {
        abortController.abort();
    });

    // clang-format off
    // get serial options as an EM_VAL
    EM_ASYNC_JS(EM_VAL, open_serial_port, (EM_VAL serialOpts), {
        const port = await
        navigator.serial.requestPort();
        await
        port.open(serialOpts);
        return port;
    });

    EM_ASYNC_JS(void, set_serial_port_options, (EM_VAL port, EM_VAL serialOpts), {
        if (port.readable && port.writable) {
            await port.close();
        }
        await port.open(serialOpts);
    });

    EM_ASYNC_JS(void, close_serial_port, (EM_VAL port), {
        await port.close();
    });

    EM_ASYNC_JS(bool, is_serial_port_open, (EM_VAL port), {
        return port.readable && port.writable;
    });

    EM_ASYNC_JS(std::vector<uint8_t>, read_data, (EM_VAL port, int numberOfBytes, int msTimeout), {
        const reader = port.readable.getReader();
        async receive() {
            const { value } = await reader.read()
            return value
        }

        const timeoutPromise = new Promise((resolve, _) => {
                setTimeout(() => {
                        resolve("Timeout");
                }, timeoutMs);
        });

        var returnBuffer = new Uint8Array(0);
        while (true) {
            const promise = this.receive();
            const result = await Promise.race([promise, timeoutPromise]);

            if (result instanceof Uint8Array) {
                if
                    return returnBuffer;
            } else if (result === "Timeout") {
                await this.uploader.readStream.cancel();
                await this.uploader.readStream.releaseLock();
                this.uploader.readStream = this.port.readable.getReader();
                throw new Error('Timeout');
            }
        }
    });

    EM_ASYNC_JS(void, clear_read_buffer, (EM_VAL port), {
        const timeoutPromise = new Promise((resolve, _) => {
                setTimeout(() => {
                        resolve("Timeout");
                }, 100);
        });
        const timeoutPromiseRead = new Promise((resolve, _) => {
                setTimeout(() => {
                        resolve("Timeout");
                }, 1500);
        });

        let i = 1;
        while (true) {
            const readStream = port.readable.getReader();
            const promise = new Promise(async (resolve, _) => {
                    while (true) {
                        const result = await Promise.race([readStream.read(), timeoutPromise]);
                        if (result === "Timeout")
                            break;
                    }
                    resolve("K");
            });
            const result = await Promise.race([promise, timeoutPromiseRead]);

            if (result !== "Timeout")
                break;
            if (i > 10)
                throw new Error('Timeout');
            i++;
        }
        readStream.releaseLock();
    });

    EM_VAL generate_serial_options(const std::map<std::string, int>& serialOptions)
    {
        val serialOpts = val::object();
        for (auto& [key, value] : serialOptions)
        {
            // for a key like flowControl, parity, etc, we need to convert the value to a string
            if (key == "flowControl")
            {
                std::string valueStr;
                switch (value)
                {
                    case static_cast<int>(LibSerial::FlowControl::FLOW_CONTROL_HARDWARE):
                        valueStr = "hardware";
                        break;
                    case static_cast<int>(LibSerial::FlowControl::FLOW_CONTROL_NONE):
                        valueStr = "none";
                        break;
                    case static_cast<int>(LibSerial::FlowControl::FLOW_CONTROL_SOFTWARE):
                        valueStr = "software";
                        break;
                    default:
                        valueStr = "none";
                        break;
                }
                serialOpts.set(key, valueStr);
            } else if (key == "parity")
            {
                std::string valueStr;
                switch (value)
                {
                    case static_cast<int>(LibSerial::Parity::PARITY_EVEN):
                        valueStr = "even";
                        break;
                    case static_cast<int>(LibSerial::Parity::PARITY_NONE):
                        valueStr = "none";
                        break;
                    case static_cast<int>(LibSerial::Parity::PARITY_ODD):
                        valueStr = "odd";
                        break;
                    default:
                        valueStr = "none";
                        break;
                }
                serialOpts.set(key, valueStr);
            } else if (key == "stopBits")
            {
                std::string valueStr;
                switch (value)
                {
                    case static_cast<int>(LibSerial::StopBits::STOP_BITS_1):
                        valueStr = "1";
                        break;
                    case static_cast<int>(LibSerial::StopBits::STOP_BITS_2):
                        valueStr = "2";
                        break;
                    default:
                        valueStr = "1";
                        break;
                }
                serialOpts.set(key, valueStr);
            }
            else
            {
                serialOpts.set(key, value);
            }
        }
        return reinterpret_cast<EM_VAL>(&serialOpts);
    }

    EM_ASYNC_JS(void, write_data, (EM_VAL port, val data), {
        const writer = await port.writable.getWriter();
        await writer.write(data);
        writer.releaseLock();
    });

    void cpp_set_serial_options(EM_VAL port ,const std::map<std::string, int>& serialOptions)
    {

        set_serial_port_options(port, generate_serial_options(serialOptions));
    }

}

namespace LibSerial
{
    /**
     * @brief SerialPort::Implementation is the SerialPort implementation class.
     */
    class SerialPort::Implementation
    {
    private:
        std::map<std::string, int> serialOptions = {};
    public:

        /**
         * @brief The read buffer for the serial port.
         */
        std::vector<uint8_t> mReadBuffer = {};

        /**
         * @brief Default Constructor.
         */
        Implementation() = default ;

        /**
         * @brief Constructor that allows a SerialPort instance to be
         *        created and opened, initializing the corresponding
         *        serial port with the specified parameters.
         * @param fileName The file name of the serial port.
         * @param baudRate The communications baud rate.
         * @param characterSize The size of the character buffer for
         *        storing read/write streams.
         * @param parityType The parity type for the serial port.
         * @param stopBits The number of stop bits for the serial port.
         * @param flowControlType The flow control type for the serial port.
         */
        Implementation(const std::string&   fileName,
                       const BaudRate&      baudRate,
                       const CharacterSize& characterSize,
                       const FlowControl&   flowControlType,
                       const Parity&        parityType,
                       const StopBits&      stopBits) ;

        /**
         * @brief Default Destructor for a SerialPort object. Closes the
         *        serial port associated with mFileDescriptor if open.
         */
        ~Implementation() noexcept ;

        /**
         * @brief Copy construction is disallowed.
         */
        Implementation(const Implementation& otherImplementation) = delete ;

        /**
         * @brief Move construction is disallowed.
         */
        Implementation(const Implementation&& otherImplementation) = delete ;

        /**
         * @brief Copy assignment is disallowed.
         */
        Implementation& operator=(const Implementation& otherImplementation) = delete ;

        /**
         * @brief Move assignment is disallowed.
         */
        Implementation& operator=(const Implementation&& otherImplementation) = delete ;

        /**
         * @brief Opens the serial port associated with the specified
         *        file name and the specified mode.
         * @param fileName The file name of the serial port.
         * @param openMode The communication mode status when the serial
         *        communication port is opened.
         */
        void Open(const std::string& fileName,
                  const std::ios_base::openmode& openMode) ;

        /**
         * @brief Closes the serial port. All settings of the serial port will be
         *        lost and no more I/O can be performed on the serial port.
         */
        void Close() ;

        /**
         * @brief Waits until the write buffer is drained and then returns.
         */
        void DrainWriteBuffer() ;

        /**
         * @brief Flushes the serial port input buffer.
         */
        void FlushInputBuffer() ;

        /**
         * @brief Flushes the serial port output buffer.
         */
        void FlushOutputBuffer() ;

        /**
         * @brief Flushes the serial port input and output buffers.
         */
        void FlushIOBuffers() ;

        /**
         * @brief Determines if data is available at the serial port.
         */
        bool IsDataAvailable() ;

        /**
         * @brief Determines if the serial port is open for I/O.
         * @return Returns true iff the serial port is open.
         */
        bool IsOpen() const ;

        /**
         * @brief Sets all serial port paramters to their default values.
         */
        void SetDefaultSerialPortParameters() ;



        /**
         * @brief Sets the baud rate for the serial port to the specified value
         * @param baudRate The baud rate to be set for the serial port.
         */
        void SetBaudRate(const BaudRate& baudRate) ;

        /**
         * @brief Gets the current baud rate for the serial port.
         * @return Returns the baud rate.
         */
        BaudRate GetBaudRate() const ;

        /**
         * @brief Sets the character size for the serial port.
         * @param characterSize The character size to be set.
         */
        void SetCharacterSize(const CharacterSize& characterSize) ;

        /**
         * @brief Gets the character size being used for serial communication.
         * @return Returns the current character size.
         */
        CharacterSize GetCharacterSize() const ;


        /**
         * @brief Sets flow control for the serial port.
         * @param flowControlType The flow control type to be set.
         */
        void SetFlowControl(const FlowControl& flowControlType) ;

        /**
         * @brief Get the current flow control setting.
         * @return Returns the flow control type of the serial port.
         */
        FlowControl GetFlowControl() const ;

        /**
         * @brief Sets the parity type for the serial port.
         * @param parityType The parity type to be set.
         */
        void SetParity(const Parity& parityType) ;

        /**
         * @brief Gets the parity type for the serial port.
         * @return Returns the parity type.
         */
        Parity GetParity() const ;

        /**
         * @brief Sets the number of stop bits to be used with the serial port.
         * @param stopBits The number of stop bits to set.
         */
        void SetStopBits(const StopBits& stopBits) ;

        /**
         * @brief Gets the number of stop bits currently being used by the serial
         * @return Returns the number of stop bits.
         */
        StopBits GetStopBits() const ;

        /**
         * @brief Sets the serial port DTR line status.
         * @param dtrState The state to set the DTR line
         */
        void SetDTR(const bool dtrState) ;

        /**
         * @brief Gets the serial port DTR line status.
         * @return Returns true iff the status of the DTR line is high.
         */
        bool GetDTR() ;

        /**
         * @brief Sets the serial port RTS line status.
         * @param rtsState The state to set the RTS line
         */
        void SetRTS(const bool rtsState) ;

        /**
         * @brief Gets the serial port RTS line status.
         * @return Returns true iff the status of the RTS line is high.
         */
        bool GetRTS() ;

        /**
         * @brief Gets the serial port CTS line status.
         * @return Returns true iff the status of the CTS line is high.
         */
        bool GetCTS() ;

        /**
         * @brief Gets the serial port DSR line status.
         * @return Returns true iff the status of the DSR line is high.
         */
        bool GetDSR() ;

        /**
         * @brief Gets the serial port file descriptor.
         * @return Returns the serial port file descriptor.
         */
        EM_VAL GetFileDescriptor() const ;

        /**
         * @brief Gets the number of bytes available in the read buffer.
         * @return Returns the number of bytes available in the read buffer.
         */
        int GetNumberOfBytesAvailable() const ;

        /**
         * @brief Reads the specified number of bytes from the serial port.
         *        The method will timeout if no data is received in the
         *        specified number of milliseconds (msTimeout). If msTimeout
         *        is zero, then the method will block until all requested bytes
         *        are received. If numberOfBytes is zero and msTimeout is
         *        non-zero,  the method will continue receiving data for the
         *        specified of milliseconds. If numberOfBytes is zero and
         *        msTimeout is zero, the method will return immediately. In all
         *        cases, any data received remains available in the dataBuffer
         *        on return from this method.
         * @param dataBuffer The data buffer to place data into.
         * @param numberOfBytes The number of bytes to read before returning.
         * @param msTimeout The timeout period in milliseconds.
         */
        void Read(DataBuffer&  dataBuffer,
                  size_t       numberOfBytes = 0,
                  size_t       msTimeout = 0) ;

        /**
         * @brief Reads the specified number of bytes from the serial port.
         *        The method will timeout if no data is received in the specified
         *        number of milliseconds (msTimeout). If msTimeout is 0, then
         *        this method will block until all requested bytes are
         *        received. If numberOfBytes is zero, then this method will keep
         *        reading data till no more data is available at the serial port.
         *        In all cases, all read data is available in dataString on
         *        return from this method.
         * @param dataString The data string read from the serial port.
         * @param numberOfBytes The number of bytes to read before returning.
         * @param msTimeout The timeout period in milliseconds.
         */
        void Read(std::string& dataString,
                  size_t       numberOfBytes = 0,
                  size_t       msTimeout = 0) ;

        /**
         * @brief Reads a single byte from the serial port.
         *        If no data is available within the specified number
         *        of milliseconds (msTimeout), then this method will
         *        throw a ReadTimeout exception. If msTimeout is 0,
         *        then this method will block until data is available.
         * @param charBuffer The character read from the serial port.
         * @param msTimeout The timeout period in milliseconds.
         */
        template <typename ByteType,
                typename = std::enable_if_t<(sizeof(ByteType) == 1)>>
        void ReadByte(ByteType&  charBuffer,
                      size_t msTimeout = 0) ;

        /**
         * @brief Reads a line of characters from the serial port.
         *        The method will timeout if no data is received in the specified
         *        number of milliseconds (msTimeout). If msTimeout is 0, then
         *        this method will block until a line terminator is received.
         *        If a line terminator is read, a string will be returned,
         *        however, if the timeout is reached, an exception will be thrown
         *        and all previously read data will be lost.
         * @param dataString The data string read from the serial port.
         * @param lineTerminator The line termination character to specify the
         *        end of a line.
         * @param msTimeout The timeout value to return if a line termination
         *        character is not read.
         */
        void ReadLine(std::string& dataString,
                      char         lineTerminator = '\n',
                      size_t       msTimeout = 0) ;

        /**
         * @brief Writes a DataBuffer to the serial port.
         * @param dataBuffer The DataBuffer to write to the serial port.
         */
        void Write(const DataBuffer& dataBuffer) ;

        /**
         * @brief Writes a std::string to the serial port.
         * @param dataString The std::string to be written to the serial port.
         */
        void Write(const std::string& dataString) ;

        /**
         * @brief Writes a single byte to the serial port.
         * @param charBuffer The byte to be written to the serial port.
         */
        void WriteByte(char charBuffer) ;

        /**
         * @brief Writes a single byte to the serial port.
         * @param charBuffer The byte to be written to the serial port.
         */
        void WriteByte(unsigned char charBuffer) ;

        /**
         * @brief Sets the current state of the serial port blocking status.
         * @param blockingStatus The serial port blocking status to be set,
         *        true if to be set blocking, false if to be set non-blocking.
         */
        void SetSerialPortBlockingStatus(bool blockingStatus) ;

        /**
         * @brief Gets the current state of the serial port blocking status.
         * @return True if port is blocking, false if port non-blocking.
         */
        bool GetSerialPortBlockingStatus() const ;

        /**
         * @brief Set the specified modem control line to the specified value.
         * @param modemLine One of the following four values: TIOCM_DTR,
         *        TIOCM_RTS, TIOCM_CTS, or TIOCM_DSR.
         * @param lineState State of the modem line after successful
         *        call to this method.
         */
        void SetModemControlLine(int modemLine,
                                 bool lineState) ;

        /**
         * @brief Get the current state of the specified modem control line.
         * @param modemLine One of the following four values: TIOCM_DTR,
         *        TIOCM_RTS, TIOCM_CTS, or TIOCM_DSR.
         * @return True if the specified line is currently set and false
         *         otherwise.
         */
        bool GetModemControlLine(int modemLine) ;

    private:

        /**
         * @brief Gets the bit rate for the serial port given the current baud rate setting.
         * @return Returns the bit rate the serial port is capable of achieving.
         */
        int GetBitRate(const BaudRate& baudRate) const ;

        /**
         * @brief Sets the default serial port input modes.
         */
        void SetDefaultInputModes() ;

        /**
         * @brief Sets the default serial port output modes.
         */
        void SetDefaultOutputModes() ;

        /**
         * @brief Sets the default serial port control modes.
         */
        void SetDefaultControlModes() ;

        /**
         * @brief Sets the default serial port local modes.
         */
        void SetDefaultLocalModes() ;

        /**
         * The file descriptor corresponding to the serial port.
         */
        _EM_VAL *mFileDescriptor = nullptr ;

        /**
         * The abort controller for the read pipe.
         */
        _EM_VAL *mReadAbortController = nullptr;

        /**
         * The time in microseconds required for a byte of data to arrive at
         * the serial port.
         */
        int mByteArrivalTimeDelta = 1 ;
    } ;

    SerialPort::SerialPort()
            : mImpl(new Implementation())
    {
        /* Empty */
    }

    SerialPort::SerialPort(const std::string&   fileName,
                           const BaudRate&      baudRate,
                           const CharacterSize& characterSize,
                           const FlowControl&   flowControlType,
                           const Parity&        parityType,
                           const StopBits&      stopBits)
            : mImpl(new Implementation(fileName,
                                       baudRate,
                                       characterSize,
                                       flowControlType,
                                       parityType,
                                       stopBits))
    {
        /* Empty */
    }

    SerialPort::SerialPort(SerialPort&& otherSerialPort) :
            mImpl(std::move(otherSerialPort.mImpl))
    {
        // empty
    }

    SerialPort& SerialPort::operator=(SerialPort&& otherSerialPort)
    {
        mImpl = std::move(otherSerialPort.mImpl);
        return *this;
    }

    SerialPort::~SerialPort() noexcept = default ;

    void
    SerialPort::Open(const std::string& fileName,
                     const std::ios_base::openmode& openMode)
    {
        mImpl->Open(fileName,
                    openMode) ;
    }

    void
    SerialPort::Close()
    {
        mImpl->Close() ;
    }

    void
    SerialPort::DrainWriteBuffer()
    {
        mImpl->DrainWriteBuffer() ;
    }

    void
    SerialPort::FlushInputBuffer()
    {
        mImpl->FlushInputBuffer() ;
    }

    void
    SerialPort::FlushOutputBuffer()
    {
        mImpl->FlushOutputBuffer() ;
    }

    void
    SerialPort::FlushIOBuffers()
    {
        mImpl->FlushIOBuffers() ;
    }

    bool
    SerialPort::IsDataAvailable()
    {
        return mImpl->IsDataAvailable() ;
    }

    bool
    SerialPort::IsOpen() const
    {
        return mImpl->IsOpen() ;
    }

    void
    SerialPort::SetDefaultSerialPortParameters()
    {
        mImpl->SetDefaultSerialPortParameters() ;
    }

    void
    SerialPort::SetBaudRate(const BaudRate& baudRate)
    {
        mImpl->SetBaudRate(baudRate) ;
    }

    BaudRate
    SerialPort::GetBaudRate() const
    {
        return mImpl->GetBaudRate() ;
    }

    void
    SerialPort::SetCharacterSize(const CharacterSize& characterSize)
    {
        mImpl->SetCharacterSize(characterSize) ;
    }

    CharacterSize
    SerialPort::GetCharacterSize() const
    {
        return mImpl->GetCharacterSize() ;
    }

    void
    SerialPort::SetFlowControl(const FlowControl& flowControlType)
    {
        mImpl->SetFlowControl(flowControlType) ;
    }

    FlowControl
    SerialPort::GetFlowControl() const
    {
        return mImpl->GetFlowControl() ;
    }

    void
    SerialPort::SetParity(const Parity& parityType)
    {
        mImpl->SetParity(parityType) ;
    }

    Parity
    SerialPort::GetParity() const
    {
        return mImpl->GetParity() ;
    }

    void
    SerialPort::SetStopBits(const StopBits& stopBits)
    {
        mImpl->SetStopBits(stopBits) ;
    }

    StopBits
    SerialPort::GetStopBits() const
    {
        return mImpl->GetStopBits() ;
    }

    void
    SerialPort::SetDTR(const bool dtrState)
    {
        mImpl->SetDTR(dtrState) ;
    }

    bool
    SerialPort::GetDTR() const
    {
        return mImpl->GetDTR() ;
    }

    void
    SerialPort::SetRTS(const bool rtsState)
    {
        mImpl->SetRTS(rtsState) ;
    }

    bool
    SerialPort::GetRTS() const
    {
        return mImpl->GetRTS() ;
    }

    bool
    SerialPort::GetCTS()
    {
        return mImpl->GetCTS() ;
    }

    bool
    SerialPort::GetDSR()
    {
        return mImpl->GetDSR() ;
    }

    _EM_VAL *
    SerialPort::GetFileDescriptor() const
    {
        return mImpl->GetFileDescriptor() ;
    }

    int
    SerialPort::GetNumberOfBytesAvailable()
    {
        return mImpl->GetNumberOfBytesAvailable() ;
    }

    void
    SerialPort::Read(DataBuffer& dataBuffer,
                     const size_t numberOfBytes,
                     const size_t msTimeout)
    {
        mImpl->Read(dataBuffer,
                    numberOfBytes,
                    msTimeout) ;
    }

    void
    SerialPort::Read(std::string& dataString,
                     const size_t numberOfBytes,
                     const size_t msTimeout)
    {
        mImpl->Read(dataString,
                    numberOfBytes,
                    msTimeout) ;
    }

    void
    SerialPort::ReadByte(char&        charBuffer,
                         const size_t msTimeout)
    {
        mImpl->ReadByte(charBuffer,
                        msTimeout) ;
    }

    void
    SerialPort::ReadByte(unsigned char& charBuffer,
                         const size_t   msTimeout)
    {
        mImpl->ReadByte(charBuffer,
                        msTimeout) ;
    }

    void
    SerialPort::ReadLine(std::string& dataString,
                         const char   lineTerminator,
                         const size_t msTimeout)
    {
        mImpl->ReadLine(dataString,
                        lineTerminator,
                        msTimeout) ;
    }

    void
    SerialPort::Write(const DataBuffer& dataBuffer)
    {
        mImpl->Write(dataBuffer) ;
    }

    void
    SerialPort::Write(const std::string& dataString)
    {
        mImpl->Write(dataString) ;
    }

    void
    SerialPort::WriteByte(const char charBuffer)
    {
        mImpl->WriteByte(charBuffer) ;
    }

    void
    SerialPort::WriteByte(const unsigned char charBuffer)
    {
        mImpl->WriteByte(charBuffer) ;
    }

    void
    SerialPort::SetSerialPortBlockingStatus(const bool blockingStatus)
    {
        mImpl->SetSerialPortBlockingStatus(blockingStatus) ;
    }

    bool
    SerialPort::GetSerialPortBlockingStatus() const
    {
        return mImpl->GetSerialPortBlockingStatus() ;
    }

    void
    SerialPort::SetModemControlLine(const int modemLine,
                                    const bool lineState)
    {
        mImpl->SetModemControlLine(modemLine, lineState) ;
    }

    bool
    SerialPort::GetModemControlLine(const int modemLine)
    {
        return mImpl->GetModemControlLine(modemLine) ;
    }

    /** -------------------------- Implementation -------------------------- */

    inline
    SerialPort::Implementation::Implementation(const std::string&   fileName,
                                               const BaudRate&      baudRate,
                                               const CharacterSize& characterSize,
                                               const FlowControl&   flowControlType,
                                               const Parity&        parityType,
                                               const StopBits&      stopBits)
    {
        this->Open(fileName, std::ios_base::in | std::ios_base::out) ;
        this->SetBaudRate(baudRate) ;
        this->SetFlowControl(flowControlType) ;
        this->SetParity(parityType) ;
        this->SetStopBits(stopBits) ;
    }

    inline
    SerialPort::Implementation::~Implementation()
    try
    {
        // Close the serial port if it is open.
        if (this->IsOpen())
        {
            this->Close() ;
        }
    }
    catch(...)
    {
        //
        // :IMPORTANT: We do not let any exceptions escape the destructor.
        // (see https://isocpp.org/wiki/faq/exceptions#dtors-shouldnt-throw)
        //
        // :TODO: Once we add logging to LibSerial, we should issue a warning
        // if we reach here.
        //
    }

    inline
    void
    SerialPort::Implementation::Open(const std::string& fileName,
                                     const std::ios_base::openmode& openMode)
    {
        if (serialOptions.empty())
        {
            serialOptions["baudRate"] = static_cast<int>(BaudRate::BAUD_DEFAULT);
            serialOptions["dataBits"] = static_cast<int>(CharacterSize::CHAR_SIZE_DEFAULT);
            serialOptions["stopBits"] = static_cast<int>(StopBits::STOP_BITS_DEFAULT);
            serialOptions["parity"] = static_cast<int>(Parity::PARITY_DEFAULT);
            serialOptions["flowControl"] = static_cast<int>(FlowControl::FLOW_CONTROL_DEFAULT);
        }

        if (fileName != "/dev/null")
        {
            throw std::runtime_error("SerialPort::Open: Only /dev/null is supported for webserial support") ;
        }


        // generate serial options from the serialOptions map
        val serialOpts = val::object();
        for (auto& [key, value] : serialOptions)
        {
            serialOpts.set(key, value);
        }
        mFileDescriptor = open_serial_port(reinterpret_cast<EM_VAL>(&serialOpts));
        mReadAbortController = start_callback(mFileDescriptor, this);
    }

    inline
    void
    SerialPort::Implementation::Close()
    {
        if (mFileDescriptor == nullptr)
        {
            return ;
        }

        if (is_serial_port_open(mFileDescriptor))
        {
            if (mReadAbortController != nullptr)
            {
                stop_callback(mReadAbortController);
            }
            close_serial_port(mFileDescriptor);
        }
    }

    inline
    void
    SerialPort::Implementation::DrainWriteBuffer()
    {
        // In the case of a serial port, there is no write buffer to drain.
        return;
    }

    inline
    void
    SerialPort::Implementation::FlushInputBuffer()
    {
        // Throw an exception if the serial port is not open.
        if (not this->IsOpen())
        {
            throw NotOpen(ERR_MSG_PORT_NOT_OPEN) ;
        }

        return;
    }

    inline
    void
    SerialPort::Implementation::FlushOutputBuffer()
    {
        // Throw an exception if the serial port is not open.
        if (not this->IsOpen())
        {
            throw NotOpen(ERR_MSG_PORT_NOT_OPEN) ;
        }

        clear_read_buffer(mFileDescriptor);
    }

    inline
    void
    SerialPort::Implementation::FlushIOBuffers()
    {
        // Throw an exception if the serial port is not open.
        if (not this->IsOpen())
        {
            throw NotOpen(ERR_MSG_PORT_NOT_OPEN) ;
        }

        this->FlushInputBuffer() ;
    }

    inline
    bool
    SerialPort::Implementation::IsOpen() const
    {
        if (mFileDescriptor == nullptr)
        {
            return false ;
        }
        return is_serial_port_open(mFileDescriptor);
    }

    inline
    bool
    SerialPort::Implementation::IsDataAvailable()
    {
        // Throw an exception if the serial port is not open.
        if (not this->IsOpen())
        {
            throw NotOpen(ERR_MSG_PORT_NOT_OPEN) ;
        }

        // no idea if there is data available
        return true ;
    }

    inline
    void
    SerialPort::Implementation::SetDefaultSerialPortParameters()
    {
        // Make sure that the serial port is open.
        if (not this->IsOpen())
        {
            throw NotOpen(ERR_MSG_PORT_NOT_OPEN) ;
        }

        SetDefaultInputModes() ;
        SetDefaultOutputModes() ;
        SetDefaultControlModes() ;
        SetDefaultLocalModes() ;

        SetBaudRate(BaudRate::BAUD_DEFAULT) ;
        SetFlowControl(FlowControl::FLOW_CONTROL_DEFAULT) ;
        SetParity(Parity::PARITY_DEFAULT) ;
        SetStopBits(StopBits::STOP_BITS_DEFAULT) ;
        SetCharacterSize(CharacterSize::CHAR_SIZE_DEFAULT) ;
    }

    inline
    void
    SerialPort::Implementation::SetBaudRate(const BaudRate& baudRate)
    {
        // Throw an exception if the serial port is not open.
        if (not this->IsOpen())
        {
            throw NotOpen(ERR_MSG_PORT_NOT_OPEN) ;
        }


        // Set the baud rate for both input and output.
        serialOptions["baudRate"] = static_cast<int>(baudRate);

        // Apply the modified settings.
        cpp_set_serial_options(mFileDescriptor, serialOptions);

        // Set the time interval value (us) required for one byte of data to arrive.
        mByteArrivalTimeDelta = (BITS_PER_BYTE * MICROSECONDS_PER_SEC) / GetBitRate(baudRate) ;
    }

    inline
    BaudRate
    SerialPort::Implementation::GetBaudRate() const
    {
        // Throw an exception if the serial port is not open.
        if (not this->IsOpen())
        {
            throw NotOpen(ERR_MSG_PORT_NOT_OPEN) ;
        }

        return serialOptions.find("baudRate") != serialOptions.end() ? static_cast<BaudRate>(serialOptions.at("baudRate")) : BaudRate::BAUD_INVALID ;
    }

    inline
    void
    SerialPort::Implementation::SetCharacterSize(const CharacterSize& characterSize)
    {
        // Throw an exception if the serial port is not open.
        if (not this->IsOpen())
        {
            throw NotOpen(ERR_MSG_PORT_NOT_OPEN) ;
        }

        serialOptions["dataBits"] = static_cast<int>(characterSize);
        cpp_set_serial_options(mFileDescriptor, serialOptions);
    }

    inline
    CharacterSize
    SerialPort::Implementation::GetCharacterSize() const
    {
        // Throw an exception if the serial port is not open.
        if (not this->IsOpen())
        {
            throw NotOpen(ERR_MSG_PORT_NOT_OPEN) ;
        }

        CharacterSize character_size = CharacterSize::CHAR_SIZE_INVALID ;
        if (serialOptions.find("dataBits") != serialOptions.end())
        {
            character_size = static_cast<CharacterSize>(serialOptions.at("dataBits")) ;
        }
        return character_size ;
    }

    inline
    int
    SerialPort::Implementation::GetBitRate(const BaudRate& baudRate) const
    {
        int baud_rate_as_int = 1 ;

        switch (baudRate)
        {
            case BaudRate::BAUD_50:
                baud_rate_as_int = 50 ;
                break ;

            case BaudRate::BAUD_75:
                baud_rate_as_int = 75 ;
                break ;

            case BaudRate::BAUD_110:
                baud_rate_as_int = 110 ;
                break ;

            case BaudRate::BAUD_134:
                baud_rate_as_int = 134 ;
                break ;

            case BaudRate::BAUD_150:
                baud_rate_as_int = 150 ;
                break ;

            case BaudRate::BAUD_200:
                baud_rate_as_int = 200 ;
                break ;

            case BaudRate::BAUD_300:
                baud_rate_as_int = 300 ;
                break ;

            case BaudRate::BAUD_600:
                baud_rate_as_int = 600 ;
                break ;

            case BaudRate::BAUD_1200:
                baud_rate_as_int = 1200 ;
                break ;

            case BaudRate::BAUD_1800:
                baud_rate_as_int = 1800 ;
                break ;

            case BaudRate::BAUD_2400:
                baud_rate_as_int = 2400 ;
                break ;

            case BaudRate::BAUD_4800:
                baud_rate_as_int = 4800 ;
                break ;

            case BaudRate::BAUD_9600:
                baud_rate_as_int = 9600 ;
                break ;

            case BaudRate::BAUD_19200:
                baud_rate_as_int = 19200 ;
                break ;

            case BaudRate::BAUD_38400:
                baud_rate_as_int = 38400 ;
                break ;

            case BaudRate::BAUD_57600:
                baud_rate_as_int = 57600 ;
                break ;

            case BaudRate::BAUD_115200:
                baud_rate_as_int = 115200 ;
                break ;

            case BaudRate::BAUD_230400:
                baud_rate_as_int = 230400 ;
                break ;
            default:
                // If an incorrect baud rate was specified, throw an exception.
                throw std::runtime_error(ERR_MSG_INVALID_BAUD_RATE) ;
        }

        return baud_rate_as_int ;
    }

    inline
    void
    SerialPort::Implementation::SetFlowControl(const FlowControl& flowControlType)
    {
        // Throw an exception if the serial port is not open.
        if (not this->IsOpen())
        {
            throw NotOpen(ERR_MSG_PORT_NOT_OPEN) ;
        }

        // Flush the input and output buffers associated with the port.
        this->FlushIOBuffers() ;

        // Set the flow control type.
        serialOptions["flowControl"] = static_cast<int>(flowControlType);
        cpp_set_serial_options(mFileDescriptor, serialOptions);
    }

    inline
    FlowControl
    SerialPort::Implementation::GetFlowControl() const
    {
        // Throw an exception if the serial port is not open.
        if (not this->IsOpen())
        {
            throw NotOpen(ERR_MSG_PORT_NOT_OPEN) ;
        }

        FlowControl flow_control = FlowControl::FLOW_CONTROL_INVALID ;
        if (serialOptions.find("flowControl") != serialOptions.end())
        {
            flow_control = static_cast<FlowControl>(serialOptions.at("flowControl")) ;
        }
        return flow_control ;
    }

    inline
    void
    SerialPort::Implementation::SetParity(const Parity& parityType)
    {
        // Throw an exception if the serial port is not open.
        if (not this->IsOpen())
        {
            throw NotOpen(ERR_MSG_PORT_NOT_OPEN) ;
        }

        // Set the parity type.
        serialOptions["parity"] = static_cast<int>(parityType);
        cpp_set_serial_options(mFileDescriptor, serialOptions);
    }

    inline
    Parity
    SerialPort::Implementation::GetParity() const
    {
        // Throw an exception if the serial port is not open.
        if (not this->IsOpen())
        {
            throw NotOpen(ERR_MSG_PORT_NOT_OPEN) ;
        }

        Parity parity = Parity::PARITY_INVALID ;
        if (serialOptions.find("parity") != serialOptions.end())
        {
            parity = static_cast<Parity>(serialOptions.at("parity")) ;
        }
        return parity ;
    }

    inline
    void
    SerialPort::Implementation::SetStopBits(const StopBits& stopBits)
    {
        // Throw an exception if the serial port is not open.
        if (not this->IsOpen())
        {
            throw NotOpen(ERR_MSG_PORT_NOT_OPEN) ;
        }

        // Set the number of stop bits.
        serialOptions["stopBits"] = static_cast<int>(stopBits);
        cpp_set_serial_options(mFileDescriptor, serialOptions);
    }

    inline
    StopBits
    SerialPort::Implementation::GetStopBits() const
    {
        // Throw an exception if the serial port is not open.
        if (not this->IsOpen())
        {
            throw NotOpen(ERR_MSG_PORT_NOT_OPEN) ;
        }

        StopBits stop_bits = StopBits::STOP_BITS_INVALID ;
        if (serialOptions.find("stopBits") != serialOptions.end())
        {
            stop_bits = static_cast<StopBits>(serialOptions.at("stopBits")) ;
        }
        return stop_bits ;
    }

    inline
    void
    SerialPort::Implementation::SetDTR(const bool dtrState)
    {
        // Throw an exception if the serial port is not open.
        if (not this->IsOpen())
        {
            throw NotOpen(ERR_MSG_PORT_NOT_OPEN) ;
        }

        this->SetModemControlLine(TIOCM_DTR,
                                  dtrState) ;
    }

    inline
    bool
    SerialPort::Implementation::GetDTR()
    {
        // Throw an exception if the serial port is not open.
        if (not this->IsOpen())
        {
            throw NotOpen(ERR_MSG_PORT_NOT_OPEN) ;
        }

        return this->GetModemControlLine(TIOCM_DTR) ;
    }

    inline
    void
    SerialPort::Implementation::SetRTS(const bool rtsState)
    {
        // Throw an exception if the serial port is not open.
        if (not this->IsOpen())
        {
            throw NotOpen(ERR_MSG_PORT_NOT_OPEN) ;
        }

        this->SetModemControlLine(TIOCM_RTS,
                                  rtsState) ;
    }

    inline
    bool
    SerialPort::Implementation::GetRTS()
    {
        // Throw an exception if the serial port is not open.
        if (not this->IsOpen())
        {
            throw NotOpen(ERR_MSG_PORT_NOT_OPEN) ;
        }

        return this->GetModemControlLine(TIOCM_RTS) ;
    }

    inline
    bool
    SerialPort::Implementation::GetCTS()
    {
        // Throw an exception if the serial port is not open.
        if (not this->IsOpen())
        {
            throw NotOpen(ERR_MSG_PORT_NOT_OPEN) ;
        }

        return this->GetModemControlLine(TIOCM_CTS) ;
    }

    inline
    bool
    SerialPort::Implementation::GetDSR()
    {
        // Throw an exception if the serial port is not open.
        if (not this->IsOpen())
        {
            throw NotOpen(ERR_MSG_PORT_NOT_OPEN) ;
        }

        return this->GetModemControlLine(TIOCM_DSR) ;
    }

    inline
    _EM_VAL *
    SerialPort::Implementation::GetFileDescriptor() const
    {
        // Throw an exception if the serial port is not open.
        if (not this->IsOpen())
        {
            throw NotOpen(ERR_MSG_PORT_NOT_OPEN) ;
        }

        return this->mFileDescriptor ;
    }

    inline
    int
    SerialPort::Implementation::GetNumberOfBytesAvailable() const
    {
        // Throw an exception if the serial port is not open.
        if (not this->IsOpen())
        {
            throw NotOpen(ERR_MSG_PORT_NOT_OPEN) ;
        }

        return mReadBuffer.size();
    }

    inline
    void
    SerialPort::Implementation::Read(DataBuffer&  dataBuffer,
                                     const size_t numberOfBytes,
                                     const size_t msTimeout)
    {
        // Throw an exception if the serial port is not open.
        if (not this->IsOpen())
        {
            throw NotOpen(ERR_MSG_PORT_NOT_OPEN) ;
        }
        if (numberOfBytes == 0 && msTimeout == 0)
        {
            return;
        }
        if (mReadBuffer.size() < numberOfBytes)
        {
            throw ReadTimeout(ERR_MSG_READ_TIMEOUT) ;
        }
        // get the number of bytes to read
        size_t number_of_bytes_to_read = std::min(numberOfBytes, mReadBuffer.size());
        // copy the data from the read buffer to the data buffer
        std::copy(mReadBuffer.begin(), mReadBuffer.begin() + number_of_bytes_to_read, dataBuffer.begin());
        // remove the data from the read buffer
        mReadBuffer.erase(mReadBuffer.begin(), mReadBuffer.begin() + number_of_bytes_to_read);
    }

    inline
    void
    SerialPort::Implementation::Read(std::string& dataString,
                                     const size_t numberOfBytes,
                                     const size_t msTimeout)
    {
        // Throw an exception if the serial port is not open.
        if (not this->IsOpen())
        {
            throw NotOpen(ERR_MSG_PORT_NOT_OPEN) ;
        }

        if ((numberOfBytes == 0) and
            (msTimeout == 0))
        {
            return ;
        }

        // Local variables.
        size_t number_of_bytes_read = 0 ;
        size_t number_of_bytes_remaining = std::max(numberOfBytes, static_cast<size_t>(1)) ;
        size_t maximum_number_of_bytes = dataString.max_size() ;

        // Clear the data buffer and reserve enough space in the buffer to store the incoming data.
        dataString.clear() ;
        dataString.resize(number_of_bytes_remaining) ;

        // Obtain the entry time.
        const auto entry_time = std::chrono::high_resolution_clock::now().time_since_epoch() ;

        while (number_of_bytes_remaining > 0)
        {
            // If insufficient space remains in the buffer, exit the loop and return .
            if (number_of_bytes_remaining >= maximum_number_of_bytes - number_of_bytes_read)
            {
                break ;
            }

            if (numberOfBytes == 0)
            {
                // Add an additional element for the read() call.
                dataString.resize(number_of_bytes_read + 1) ;
            }

            // Read the data from the serial port.
            this->ReadByte(dataString[number_of_bytes_read],
                           msTimeout) ;

            // Allow sufficient time for an additional byte to arrive.
            usleep(mByteArrivalTimeDelta) ;
        }
    }

    template <typename ByteType, typename /* unused */>
    inline
    void
    SerialPort::Implementation::ReadByte(ByteType&  charBuffer,
                                         const size_t msTimeout)
    {
        // Double check to make sure that ByteType is exactly one byte long.
        static_assert(sizeof(ByteType) == 1,
                      "ByteType must have a size of exactly one byte.") ;

        // Throw an exception if the serial port is not open.
        if (not this->IsOpen())
        {
            throw NotOpen(ERR_MSG_PORT_NOT_OPEN) ;
        }

        // Obtain the entry time.
        const auto entry_time = std::chrono::high_resolution_clock::now().time_since_epoch() ;

        // Loop until the number of bytes requested have been read or the
        // timeout has elapsed.

        // start timer for timeout
        std::chrono::high_resolution_clock::duration elapsed_time = std::chrono::high_resolution_clock::now().time_since_epoch() - entry_time;
        ssize_t read_result = 0 ;
        while (read_result < 1)
        {
            // Check if the timeout has elapsed.
            elapsed_time = std::chrono::high_resolution_clock::now().time_since_epoch() - entry_time;
            if (msTimeout > 0 &&
                std::chrono::duration_cast<std::chrono::milliseconds>(elapsed_time).count() > msTimeout)
            {
                throw ReadTimeout(ERR_MSG_READ_TIMEOUT) ;
            }

            // Check if there is data in mReadBuffer
            if (mReadBuffer.size() > 0)
            {
                charBuffer = mReadBuffer[0];
                mReadBuffer.erase(mReadBuffer.begin());
                return;
            }

            // Allow sufficient time for an additional byte to arrive.
            usleep(mByteArrivalTimeDelta) ;
        }
    }

    inline
    void
    SerialPort::Implementation::ReadLine(std::string& dataString,
                                         const char   lineTerminator,
                                         const size_t msTimeout)
    {
        // Throw an exception if the serial port is not open.
        if (not this->IsOpen())
        {
            throw NotOpen(ERR_MSG_PORT_NOT_OPEN) ;
        }

        // Clear the data string.
        dataString.clear() ;

        unsigned char next_char = 0 ;

        size_t elapsed_ms = 0 ;

        ssize_t remaining_ms = 0 ;

        std::chrono::high_resolution_clock::duration entry_time ;
        std::chrono::high_resolution_clock::duration current_time ;
        std::chrono::high_resolution_clock::duration elapsed_time ;

        // Obtain the entry time.
        entry_time = std::chrono::high_resolution_clock::now().time_since_epoch() ;

        while (next_char != lineTerminator)
        {
            // Obtain the current time.
            current_time = std::chrono::high_resolution_clock::now().time_since_epoch() ;

            // Calculate the time delta.
            elapsed_time = current_time - entry_time ;

            // Calculate the elapsed number of milliseconds.
            elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed_time).count() ;

            // If more than msTimeout milliseconds have elapsed while
            // waiting for data, then we throw a ReadTimeout exception.
            if (msTimeout > 0 &&
                elapsed_ms > msTimeout)
            {
                throw ReadTimeout(ERR_MSG_READ_TIMEOUT) ;
            }

            remaining_ms = msTimeout - elapsed_ms ;

            this->ReadByte(next_char,
                           remaining_ms) ;

            dataString += next_char ;
        }
    }

    inline
    void
    SerialPort::Implementation::Write(const DataBuffer& dataBuffer)
    {
        // Throw an exception if the serial port is not open.
        if (not this->IsOpen())
        {
            throw NotOpen(ERR_MSG_PORT_NOT_OPEN) ;
        }

        size_t number_of_bytes = dataBuffer.size() ;

        // Nothing needs to be done if there is no data in the string.
        if (number_of_bytes <= 0)
        {
            return ;
        }

        // turn the std::vector<uint8_t> into an UInt8Array for javascript
        val data = val::global("Uint8Array").new_(val::array());
        for (size_t i = 0; i < number_of_bytes; i++)
        {
            data.set(i, dataBuffer[i]);
        }
        write_data(mFileDescriptor, data);
    }

    inline
    void
    SerialPort::Implementation::Write(const std::string& dataString)
    {
        // Throw an exception if the serial port is not open.
        if (not this->IsOpen())
        {
            throw NotOpen(ERR_MSG_PORT_NOT_OPEN) ;
        }

        // convert the string to a std::vector<uint8_t>
        DataBuffer dataBuffer(dataString.begin(), dataString.end());
        this->Write(dataBuffer);
    }

    inline
    void
    SerialPort::Implementation::WriteByte(const char charBuffer)
    {
        // Throw an exception if the serial port is not open.
        if (not this->IsOpen())
        {
            throw NotOpen(ERR_MSG_PORT_NOT_OPEN) ;
        }

        // convert the char to a std::vector<uint8_t>
        DataBuffer dataBuffer(1, charBuffer);
        this->Write(dataBuffer);
    }

    inline
    void
    SerialPort::Implementation::WriteByte(const unsigned char charBuffer)
    {
        // Throw an exception if the serial port is not open.
        if (not this->IsOpen())
        {
            throw NotOpen(ERR_MSG_PORT_NOT_OPEN) ;
        }

        // convert the char to a std::vector<uint8_t>
        DataBuffer dataBuffer(1, charBuffer);
        this->Write(dataBuffer);
    }
} // namespace LibSerial
