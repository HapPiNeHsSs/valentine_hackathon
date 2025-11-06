import asyncio
from dotenv import load_dotenv
import logging
import os, io
import requests
import wave
# import vosk
# import whisper
import numpy as np
from deepgram.utils import verboselogs
# from pydub import AudioSegment
from aiortc import RTCDataChannel
from deepgram import (
    DeepgramClient,
    ClientOptionsFromEnv,
    DeepgramClientOptions,
    SpeakWebSocketEvents,
    SpeakWSOptions,
    SpeakOptions,
    PrerecordedOptions,
    FileSource,
)

# model = whisper.load_model("turbo")
# model_path = "vosk-model-en-us-0.22"
# model = vosk.Model(model_path)
# rec = vosk.KaldiRecognizer(model, 16000)

load_dotenv() 
DEEPGRAM_API_KEY = os.getenv("DEEPGRAM_API_KEY")
DEEPGRAM_URL = "https://api.deepgram.com/v1/speak?encoding=linear16&model=aura-asteria-en&sample_rate=16000"

headers = {
    "Authorization": f"Token {DEEPGRAM_API_KEY}",
    "Content-Type": "application/json"
}


class DeepgramTTS():
  
    def __init__(self):

        self.deepgram = DeepgramClient(api_key="", config=ClientOptionsFromEnv(options={"auto_flush_speak_delta":True}))
        self.dg_connection = self.deepgram.speak.websocket.v("1")        
        self.dg_connection.on(SpeakWebSocketEvents.AudioData, self.on_audio)  
        self.dg_connection.on(SpeakWebSocketEvents.Flushed, self.on_flush)
        self.dg_connection.on(SpeakWebSocketEvents.Cleared, self.on_clear)
        self.dg_connection.on(SpeakWebSocketEvents.Close, self.on_close)
        self.dg_connection.on(SpeakWebSocketEvents.Error, self.on_error)
        self.dg_connection.on(SpeakWebSocketEvents.Warning, self.on_warning)
        self.dg_connection.on(SpeakWebSocketEvents.Unhandled, self.on_unhandled)
        self.audio_datachannel = None
        self.video_datachannel = None
        # connect to websocket
        options = SpeakWSOptions(
            model="aura-asteria-en",
            encoding="linear16",
            sample_rate=48000,
        )
        self.dg_connection.start(options)
    
    def speak(self, text):
        payload = {
            "text": text
        }
        response = requests.post(DEEPGRAM_URL, headers=headers, json=payload, stream=True)
        res = []
        for chunk in response.iter_content(chunk_size=1024):
            if chunk:
                res.append(chunk)
        return res

    def transcribe(self, audio):
        #result = rec.AcceptWaveform(audio)
        
        # if rec.AcceptWaveform(audio):#accept waveform of input voice
        #     # Parse the JSON result and get the recognized text
        #     result = json.loads(rec.Result())
        #     print(result)
        #     recognized_text = result['text']
        #     print(recognized_text)
        #     return recognized_text
        # else:
        #     return ""

        # np_data = np.frombuffer(audio, np.int16).flatten().astype(np.float32) / 32768.0
        
        # result =   model.transcribe(np_data, language="en", fp16=False, verbose = True)
        # print(result)
        # return result["text"]
        
        #using Deepgram
        a = io.BytesIO()
        wav = wave.open("test.wav", mode='wb')
        # wav = wave.Wave_write
        
        wav.setnchannels(2)
        wav.setframerate(16000)
        wav.setsampwidth(2)
        wav.writeframes(audio)
        wav.close()
        
        with open("test.wav", "rb") as file:
            buffer_data = file.read()
            
        payload: FileSource = {
            "buffer": buffer_data,
        }
    

        #STEP 2: Configure Deepgram options for audio analysis
        options = PrerecordedOptions(
            model="nova-3-general",
             encoding="linear16",
            sample_rate=48000,

        )
        # STEP 3: Call the transcribe_file method with the text payload and options
        
        
        response = self.deepgram.listen.rest.v("1").transcribe_file(payload, options)
        # STEP 4: Print the response
        print(response.results.channels[0].alternatives[0].transcript)
        return(response.results.channels[0].alternatives[0].transcript)
        

    def on_open(self, lwsc, open, **kwargs):
        print(f"\n\n{open}\n\n")

    import struct, math

    SHORT_NORMALIZE = (1.0/32768.0)
    swidth = 2
    
    def on_audio(self, swsc, data, **kwargs):
        #print(f"bytes: {data}")
        print("RTC Channel Label and Status")
            
        loop = asyncio.new_event_loop() 
        asyncio.set_event_loop(loop)
        #upsampled_audio = dg_stt.resample_audio(data, 16000, 44100)
        self.datachannel.send(data)

    
    # def set_output_callback(self, callback):
    #     self.dg_connection.on(SpeakWebSocketEvents.AudioData, callback)        
    def set_audio_data_channel(self, channel):
        self.audio_datachannel = channel        
    
    def set_video_data_channel(self, channel):
        self.video_datachannel = channel        

    def rms(self, frame): #Root mean Square: a function to check if the audio is silent. Commonly used in Audio stuff
        count = len(frame) / swidth
        format = "%dh" % (count) 
        #print(format)
        #print(frame)
        shorts = struct.unpack(format, frame) #unpack a frame into individual Decimal Value
        print(shorts)
        sum_squares = 0.0
        for sample in shorts:
            n = sample * SHORT_NORMALIZE #get the level of a sample and normalize it a bit (increase levels)
            sum_squares += n * n #get square of level
        rms = math.pow(sum_squares / count, 0.5) #summ all levels and get mean
        return rms * 1000 #raise value a bit so it's easy to read 
    
    def resample_audio(self, audiobytes, original_samples, desired_samples, sample_width=2, channels=2):
        sound = AudioSegment(
            # raw audio data (bytes)
            data=audiobytes,

            # 2 byte (16 bit) samples
            sample_width=sample_width,

            # 44.1 kHz frame rate
            frame_rate=original_samples,

            # stereo
            channels=channels
        )
        print(sound.frame_rate)
        
        ds = sound.set_frame_rate(desired_samples)
        print(ds.frame_rate)
        return ds.raw_data
        
        


    def on_metadata(self, swsc, metadata, **kwargs):
            print(f"\n\n{metadata}\n\n")
    def on_flush(self, swsc, flushed, **kwargs):
        print(f"\n\n{flushed}\n\n")
    def on_clear(self, swsc, clear, **kwargs):
        print(f"\n\n{clear}\n\n")
    def on_close(self, swsc, close, **kwargs):
        print(f"\n\n{close}\n\n")
    def on_warning(self, swsc, warning, **kwargs):
        print(f"\n\n{warning}\n\n")
    def on_error(self, swsc, error, **kwargs):
        print(f"\n\n{error}\n\n")
    def on_unhandled(self, swsc, unhandled, **kwargs):
        print(f"\n\n{unhandled}\n\n")

