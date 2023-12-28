// MFC
#include "windows.h"

#include "dxsound.h"
#include "main.h"
#include "pcejin.h"

#pragma comment(lib,"dxguid.lib")

// DirectSound8
#include <dsound.h>

//bool soundBufferLow;

class DirectSound : public SoundDriver
{
private:
	LPDIRECTSOUND8       pDirectSound; // DirectSound interface
	LPDIRECTSOUNDBUFFER  dsbPrimary;   // Primary DirectSound buffer
	LPDIRECTSOUNDBUFFER  dsbSecondary; // Secondary DirectSound buffer
	LPDIRECTSOUNDNOTIFY8 dsbNotify;
	HANDLE               dsbEvent;
	WAVEFORMATEX         wfx;          // Primary buffer wave format
	int					 soundBufferLen;
	int                  soundBufferTotalLen;
	unsigned int         soundNextPosition;

public:
	DirectSound();
	virtual ~DirectSound();

	bool init(long sampleRate);   // initialize the primary and secondary sound buffer
	void pause();  // pause the secondary sound buffer
	void reset();  // stop and reset the secondary sound buffer
	void resume(); // resume the secondary sound buffer
	void write(u16 * finalWave, int length);  // write the emulated sound to the secondary sound buffer
	void mute();
	void unMute();
	void doUserMute();
};

DirectSound::DirectSound()
{
	pDirectSound  = NULL;
	dsbPrimary    = NULL;
	dsbSecondary  = NULL;
	dsbNotify     = NULL;
	dsbEvent      = NULL;

	soundBufferTotalLen = 14700;
	soundNextPosition = 0;
}


DirectSound::~DirectSound()
{
	if(dsbNotify) {
		dsbNotify->Release();
		dsbNotify = NULL;
	}

	if(dsbEvent) {
		CloseHandle(dsbEvent);
		dsbEvent = NULL;
	}

	if(pDirectSound) {
		if(dsbPrimary) {
			dsbPrimary->Release();
			dsbPrimary = NULL;
		}

		if(dsbSecondary) {
			dsbSecondary->Release();
			dsbSecondary = NULL;
		}

		pDirectSound->Release();
		pDirectSound = NULL;
	}
}

struct EnumCallbackState
{
	LPGUID DesiredGuids[3];
	GUID Storage[3];
};

static BOOL CALLBACK
EnumerationCallback(
   LPGUID Guid,
   LPCTSTR Description,
   LPCTSTR DriverName,
   PVOID Context
)
{
	EnumCallbackState *State = (EnumCallbackState*)Context;
	static const char *Devices[] =
	{
		"(Rift Audio)",
		"(Rift S)",
		"(Oculus Virtual Audio Device)",
		NULL
	};

	for (const char **p = Devices; *p; ++p)
	{
		int idx = p - Devices;
		if (State->DesiredGuids[idx])
			continue;
		if (strstr(Description, *p))
		{
			State->DesiredGuids[idx] = State->Storage + idx;
			*State->DesiredGuids[idx] = *Guid;
		}
	}

	return TRUE;
}

static GUID
FindOculusSoundDevice()
{
	EnumCallbackState state = {0};
	DirectSoundEnumerate(EnumerationCallback, &state);
	for (int i = 0; i < sizeof(state.DesiredGuids) / sizeof(*state.DesiredGuids); ++i)
	if (state.DesiredGuids[i])
		return *state.DesiredGuids[i];
	return DSDEVID_DefaultPlayback;
}

bool DirectSound::init(long sampleRate)
{
	HRESULT hr;
	DWORD freq;
	DSBUFFERDESC dsbdesc;
	int i;
	GUID desiredDevice;

	hr = CoCreateInstance( CLSID_DirectSound8, NULL, CLSCTX_INPROC_SERVER, IID_IDirectSound8, (LPVOID *)&pDirectSound );
	if( hr != S_OK ) {
		printf("IDS_CANNOT_CREATE_DIRECTSOUND"); 
//		systemMessage( IDS_CANNOT_CREATE_DIRECTSOUND, NULL, hr );
		return false;
	}

	desiredDevice = FindOculusSoundDevice();

	pDirectSound->Initialize( &desiredDevice );
	if( hr != DS_OK ) {
		printf("IDS_CANNOT_CREATE_DIRECTSOUND"); 
	//	systemMessage( IDS_CANNOT_CREATE_DIRECTSOUND, NULL, hr );
		return false;
	}

	if( FAILED( hr = pDirectSound->SetCooperativeLevel( g_hWnd, DSSCL_EXCLUSIVE ) ) ) {
		printf("IDS_CANNOT_SETCOOPERATIVELEVEL"); 
	//	systemMessage( IDS_CANNOT_SETCOOPERATIVELEVEL, _T("Cannot SetCooperativeLevel %08x"), hr );
		return false;
	}


	// Create primary sound buffer
	ZeroMemory( &dsbdesc, sizeof(DSBUFFERDESC) );
	dsbdesc.dwSize = sizeof(DSBUFFERDESC);
	dsbdesc.dwFlags = DSBCAPS_PRIMARYBUFFER;
//	if( theApp.dsoundDisableHardwareAcceleration ) {
		dsbdesc.dwFlags |= DSBCAPS_LOCSOFTWARE;
//	}

	if( FAILED( hr = pDirectSound->CreateSoundBuffer( &dsbdesc, &dsbPrimary, NULL ) ) ) {
		printf("IDS_CANNOT_SETCOOPERATIVELEVEL"); 
//		systemMessage(IDS_CANNOT_CREATESOUNDBUFFER, _T("Cannot CreateSoundBuffer %08x"), hr);
		return false;
	}

	freq = sampleRate;
	// calculate the number of samples per frame first
	// then multiply it with the size of a sample frame (16 bit * stereo)
	// soundBufferLen previous value: 3628-120.
	// Recalc: ((freq / (20000000 / (259 * 384 * 4))) * 4) & 0xFFFFFC = 3508.
	// & FFFFFC ~= (3508.8 -%= 4) = 3508. 3509 won't work, modulus by 4.
	soundBufferLen = 3508;//( freq / 60 ) * 4;
	soundBufferTotalLen = soundBufferLen * 10;
	soundNextPosition = 0;

	ZeroMemory( &wfx, sizeof(WAVEFORMATEX) );
	wfx.wFormatTag = WAVE_FORMAT_PCM;
	wfx.nChannels = 2;
	wfx.nSamplesPerSec = freq;
	wfx.wBitsPerSample = 16;
	wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
	wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

	if( FAILED( hr = dsbPrimary->SetFormat( &wfx ) ) ) {
		printf("IDS_CANNOT_SETCOOPERATIVELEVEL"); 
	//	systemMessage( IDS_CANNOT_SETFORMAT_PRIMARY, _T("CreateSoundBuffer(primary) failed %08x"), hr );
		return false;
	}


	// Create secondary sound buffer
	ZeroMemory( &dsbdesc, sizeof(DSBUFFERDESC) );
	dsbdesc.dwSize = sizeof(DSBUFFERDESC);
	dsbdesc.dwFlags = DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_CTRLPOSITIONNOTIFY | DSBCAPS_GLOBALFOCUS | DSBCAPS_CTRLVOLUME;
//	if( theApp.dsoundDisableHardwareAcceleration ) {
		dsbdesc.dwFlags |= DSBCAPS_LOCSOFTWARE;
//	}
	dsbdesc.dwBufferBytes = soundBufferTotalLen;
	dsbdesc.lpwfxFormat = &wfx;

	if( FAILED( hr = pDirectSound->CreateSoundBuffer( &dsbdesc, &dsbSecondary, NULL ) ) ) {
		printf("IDS_CANNOT_SETCOOPERATIVELEVEL"); 
	//	systemMessage( IDS_CANNOT_CREATESOUNDBUFFER, _T("CreateSoundBuffer(secondary) failed %08x"), hr );
		return false;
	}

	if( FAILED( hr = dsbSecondary->SetCurrentPosition( 0 ) ) ) {
		printf("IDS_CANNOT_SETCOOPERATIVELEVEL"); 
	//	systemMessage( 0, _T("dsbSecondary->SetCurrentPosition failed %08x"), hr );
		return false;
	}


	if( SUCCEEDED( hr = dsbSecondary->QueryInterface( IID_IDirectSoundNotify8, (LPVOID*)&dsbNotify ) ) ) {
		dsbEvent = CreateEvent( NULL, FALSE, FALSE, NULL );
		DSBPOSITIONNOTIFY notify[10];
		for( i = 0; i < 10; i++ ) {
			notify[i].dwOffset = i * soundBufferLen;
			notify[i].hEventNotify = dsbEvent;
		}

		if( FAILED( dsbNotify->SetNotificationPositions( 10, notify ) ) ) {
			dsbNotify->Release();
			dsbNotify = NULL;
			CloseHandle(dsbEvent);
			dsbEvent = NULL;
		}
	}


	// Play primary buffer
	if( FAILED( hr = dsbPrimary->Play( 0, 0, DSBPLAY_LOOPING ) ) ) {
		printf("IDS_CANNOT_SETCOOPERATIVELEVEL"); 
	//	systemMessage( IDS_CANNOT_PLAY_PRIMARY, _T("Cannot Play primary %08x"), hr );
		return false;
	}

	return true;
}


void DirectSound::pause()
{
	if( dsbSecondary == NULL ) return;

	DWORD status;

	dsbSecondary->GetStatus( &status );

	if( status & DSBSTATUS_PLAYING ) dsbSecondary->Stop();
}


void DirectSound::reset()
{
	if( dsbSecondary == NULL ) return;

	dsbSecondary->Stop();

	dsbSecondary->SetCurrentPosition( 0 );

	soundNextPosition = 0;
}


void DirectSound::resume()
{
	if( dsbSecondary == NULL ) return;

	dsbSecondary->Play( 0, 0, DSBPLAY_LOOPING );
}
bool soundPaused = false;

void DirectSound::write(u16 * finalWave, int length)
{
	if(!pDirectSound) return;


	HRESULT      hr;
	DWORD        status = 0;
	DWORD        play = 0;
	LPVOID       lpvPtr1;
	DWORD        dwBytes1 = 0;
	LPVOID       lpvPtr2;
	DWORD        dwBytes2 = 0;

	if( !pcejin.fastForward ) {//!speedup && synchronize && !theApp.throttle
		hr = dsbSecondary->GetStatus(&status);
		if( status & DSBSTATUS_PLAYING ) {
			if( !soundPaused ) {
				while( true ) {
				  dsbSecondary->GetCurrentPosition(&play, NULL);
				  int BufferLeft = ((soundNextPosition <= play) ?
				  play - soundNextPosition :
				  soundBufferTotalLen - soundNextPosition + play);

				  //If this was threaded, it likely wouldn't cause any trouble to make it
				  //wait a little bit longer than 10 milliseconds
				  //This coding causes the game to skip a little, depending on the
				  //soundBufferTotalLen and WaitForSingleObject time.
				  if((BufferLeft < soundBufferLen * 9) || soundDriver->userMute)
				  {
					break;
				  } else {
				   if(dsbEvent) {
				    WaitForSingleObject(dsbEvent, 10);
				   }
				  }
				}
			}
		}/* else {
		 // TODO: remove?
			 setsoundPaused(true);
		}*/
	}


	// Obtain memory address of write block.
	// This will be in two parts if the block wraps around.
	if( DSERR_BUFFERLOST == ( hr = dsbSecondary->Lock(
		soundNextPosition,
		soundBufferLen,
		&lpvPtr1,
		&dwBytes1,
		&lpvPtr2,
		&dwBytes2,
		0 ) ) ) {
			// If DSERR_BUFFERLOST is returned, restore and retry lock.
			dsbSecondary->Restore();
			hr = dsbSecondary->Lock(
				soundNextPosition,
				soundBufferLen,
				&lpvPtr1,
				&dwBytes1,
				&lpvPtr2,
				&dwBytes2,
				0 );
	}

	soundNextPosition += soundBufferLen;
	soundNextPosition = soundNextPosition % soundBufferTotalLen;

	if( SUCCEEDED( hr ) ) {
		// Write to pointers.
		CopyMemory( lpvPtr1, finalWave, dwBytes1 );
		if ( lpvPtr2 ) {
			CopyMemory( lpvPtr2, finalWave + dwBytes1, dwBytes2 );
		}

		// Release the data back to DirectSound.
		hr = dsbSecondary->Unlock( lpvPtr1, dwBytes1, lpvPtr2, dwBytes2 );
	} else {
		printf("dsbSecondary->Lock() failed:");
	//	systemMessage( 0, _T("dsbSecondary->Lock() failed: %08x"), hr );
		return;
	}
}

SoundDriver *newDirectSound()
{
	return new DirectSound();
}

void DirectSound::mute()
{
	IDirectSoundBuffer8_SetVolume (dsbSecondary, DSBVOLUME_MIN);
	
}

void DirectSound::unMute() {

	IDirectSoundBuffer8_SetVolume (dsbSecondary, DSBVOLUME_MAX);
		
}

void DirectSound::doUserMute() {

	if(soundDriver->userMute) {
		soundDriver->unMute();
		soundDriver->userMute = false;
	}
	else {
		soundDriver->mute();
		soundDriver->userMute = true;
	}
}