#include "Frame.h"
#include <exception>

namespace ofxBlackmagic {
	//----------
	Frame::Frame() {
		this->data = 0;
	}

	//----------
	Frame::~Frame() {
		this->deallocate();
	}

	//----------
	void Frame::allocate(int width, int height, int channels) {
		this->deallocate();
		//we offset to store 1 byte either side of RGB so we can do ARGB and RGBA addressing
		this->data = new unsigned char[width * height * channels + 1];
		this->pixels.setFromExternalPixels(this->data + 1, width, height, channels);
		*(data + this->pixels.size()) = 255; // set alpha channel of last byte
	}

	//----------
	void Frame::deallocate() {
		if (this->data != 0) {
			delete[] this->data;
			this->data = 0;
		}
	}

	//----------
	void Frame::copyFromFrame(IDeckLinkVideoFrame* inputFrame, bool convertColor) {
		auto inputFormat = inputFrame->GetPixelFormat();
		
		if (convertColor) {
			if (inputFrame->GetWidth() != this->GetWidth() || inputFrame->GetHeight() != this->GetHeight()) {
				this->allocate(inputFrame->GetWidth(), inputFrame->GetHeight(), 4);
			}
			switch (inputFormat)
			{
			case bmdFormat8BitYUV:
			{
				auto & converter = Utils::CoManager::X().getVideoConverter();
				converter.ConvertFrame(inputFrame, this);
			}
			break;
			case bmdFormat8BitARGB:
			{
				void* dataIn;
				inputFrame->GetBytes(&dataIn);
				auto size = this->GetRowBytes() * this->GetHeight();
				memcpy(this->data, dataIn, size); //offset by 1 ARGB -> RGBA
			}
			break;
			case bmdFormat8BitBGRA:
			case bmdFormat10BitYUV:
			{
				auto & converter = Utils::CoManager::X().getVideoConverter();
				converter.ConvertFrame(inputFrame, this);
			}
			break;
			case bmdFormat10BitRGB:
			case bmdFormat10BitRGBXLE:
			case bmdFormat10BitRGBX:
			{
				auto & converter = Utils::CoManager::X().getVideoConverter();
				converter.ConvertFrame(inputFrame, this);
			}
			break;
			default:
				break;
			}
		}
		else {
			switch (inputFormat)
			{
			case bmdFormat8BitYUV:
			{
				if (inputFrame->GetWidth() != this->GetWidth() || inputFrame->GetHeight() != this->GetHeight()) {
					this->allocate(inputFrame->GetWidth(), inputFrame->GetHeight(), 2);
				}
				void* dataIn;
				inputFrame->GetBytes(&dataIn);
				auto size = this->GetWidth() * this->GetHeight() * 2; // Todo
				memcpy(this->data + 1, dataIn, size);
			}
			break;
			case bmdFormat8BitARGB:
			{
				if (inputFrame->GetWidth() != this->GetWidth() || inputFrame->GetHeight() != this->GetHeight()) {
					this->allocate(inputFrame->GetWidth(), inputFrame->GetHeight(), 4);
				}
				void* dataIn;
				inputFrame->GetBytes(&dataIn);
				auto size = this->GetRowBytes() * this->GetHeight();
				memcpy(this->data, dataIn, size); //offset by 1 ARGB -> RGBA
			}
			break;
			default:
				ofLogError() << "does not support 10bit color space without color conversion";
				break;
			}
		}

		//get timecode
		IDeckLinkTimecode * timecode = nullptr;
#if defined(_WIN32)
		auto result = inputFrame->GetTimecode(BMDTimecodeFormat::bmdTimecodeRP188Any, &timecode);
#elif defined(__APPLE_CC__)
		auto result = inputFrame->GetTimecode(_BMDTimecodeFormat::bmdTimecodeRP188Any, &timecode);
#endif
		if (result != S_OK) {
			this->timecode = {
				0, 0, 0, 0, false
			};
		} else {
			timecode->GetComponents(&this->timecode.hours, &this->timecode.minutes, &this->timecode.seconds, &this->timecode.frames);
			this->timecode.b_drop_frame = timecode->GetFlags() == bmdTimecodeIsDropFrame;
		}

		//inputFrame->GetAncillaryData(&this->ancillary);
	}
	//----------
	void Frame::swapFrame(Frame & fr)
	{
		std::swap(this->timecode, fr.timecode);
		this->pixels.swap(fr.pixels);
		std::swap(this->data, fr.data);
	}
	
	//----------
	int Frame::getWidth() const {
		return this->pixels.getWidth();
	}

	//----------
	int Frame::getHeight() const {
		return this->pixels.getHeight();
	}

	//----------
	int Frame::getNumChannels() const
	{
		return this->pixels.getNumChannels();
	}

	//----------
	const Frame::Timecode & Frame::getTimecode() const {
		return this->timecode;
	}

	//----------
	ofPixels & Frame::getPixels() {
		return this->pixels;
	}

	//----------
	const ofPixels & Frame::getPixels() const {
		return this->pixels;
	}

	//----------
	long Frame::GetWidth() {
		return this->pixels.getWidth();
	}

	//----------
	long Frame::GetHeight() {
		return this->pixels.getHeight();
	}

	//----------
	long Frame::GetRowBytes() {
		return this->GetWidth() * 4;
	}

	//----------
	BMDPixelFormat Frame::GetPixelFormat() {
		return bmdFormat8BitARGB;
	}

	//----------
	BMDFrameFlags Frame::GetFlags() {
		return 0;
	}

	//----------
	HRESULT Frame::GetBytes(void **buffer) {
		*buffer = this->data;
		return S_OK;
	}

	//----------
	HRESULT Frame::GetTimecode(BMDTimecodeFormat format, IDeckLinkTimecode **timecode) {
		throw(std::runtime_error("GetTimecode feature not supported at present"));
	}

	//----------
    HRESULT Frame::GetAncillaryData(IDeckLinkVideoFrameAncillary **ancillary) {
		*ancillary = this->ancillary;
		return S_OK;
	}

	//----------
#if defined(_WIN32)
	HRESULT Frame::QueryInterface(REFIID riid, _COM_Outptr_ void __RPC_FAR *__RPC_FAR *ppvObject) {
		return S_OK;
	}
#elif defined(__APPLE_CC__)
	HRESULT Frame::QueryInterface(REFIID iid, LPVOID *ppv) {
		return S_OK;
	}
#endif


	//----------
	ULONG Frame::AddRef() {
		return ++this->references;
	}

	//----------
	ULONG STDMETHODCALLTYPE Frame::Release() {
		return --this->references;
	}

	int Frame::Timecode::toFrameNum(int timebase) const
	{
		int num = frames + timebase * seconds + timebase * 60 * minutes + timebase * 3600 * hours;
		if (b_drop_frame) {
			num -= hours * 108;
			for (int i = 0; i <= minutes; ++i) {
				if (i % 10 != 0) {
					num -= 2;
				}
			}
		}
		return num;
	}

	string Frame::Timecode::toString() const
	{
		if (b_drop_frame) {
			return ofVAArgsToString("%02d:%02d:%02d;%02d",
				hours,
				minutes,
				seconds,
				frames);
		}
		else {
			return ofVAArgsToString("%02d:%02d:%02d:%02d",
				hours,
				minutes,
				seconds,
				frames);
		}
	}

	bool Frame::Timecode::operator==(const Timecode & vec) const
	{
		return (hours == vec.hours)
			&& (minutes == vec.minutes)
			&& (seconds == vec.seconds)
			&& (frames == vec.frames);
	}

	bool Frame::Timecode::operator!=(const Timecode & vec) const
	{
		return (hours != vec.hours)
			|| (minutes != vec.minutes)
			|| (seconds != vec.seconds)
			|| (frames != vec.frames);
	}

}

//----------
ostream & operator<<(ostream & os, const ofxBlackmagic::Frame::Timecode & timecode) {
	os << (int) timecode.hours << "h" << (int) timecode.minutes << "m" << (int) timecode.seconds << "s" << (int) timecode.frames;
	return os;
}