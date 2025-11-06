import asyncio
from dotenv import load_dotenv
import logging
from deepgram.utils import verboselogs
from pydub import AudioSegment

from deepgram import (
    DeepgramClient,
    ClientOptionsFromEnv,
    SpeakOptions,
    LiveOptions,
    LiveTranscriptionEvents,
)
load_dotenv() 
class DeepgramSTT():
  
    def __init__(self):

        deepgram = DeepgramClient(api_key="", config=ClientOptionsFromEnv())
        self.dg_connection = deepgram.listen.websocket.v("1")
        self.dg_connection.on(LiveTranscriptionEvents.Open, self.on_open)

        self.dg_connection.on(LiveTranscriptionEvents.Metadata, self.on_metadata)
        self.dg_connection.on(LiveTranscriptionEvents.SpeechStarted, self.on_speech_started)
        self.dg_connection.on(LiveTranscriptionEvents.UtteranceEnd, self.on_utterance_end)
        self.dg_connection.on(LiveTranscriptionEvents.Error, self.on_error)
        self.dg_connection.on(LiveTranscriptionEvents.Close, self.on_close)
        options: LiveOptions = LiveOptions(
            model="nova-3",
            punctuate=True,
            language="en-US",
            encoding="linear16",
            channels=2,
            sample_rate=16000,
            ## To get UtteranceEnd, the following must be set:
            interim_results=True,
            utterance_end_ms="1000",
            vad_events=True,
        )
        self.dg_connection.start(options)
        
    def on_open(self, lwsc, open, **kwargs):
        print(f"\n\n{open}\n\n")

    import struct, math

    SHORT_NORMALIZE = (1.0/32768.0)
    swidth = 2

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
    
    def resample_audio(self, audiobytes, original_samples, desired_samples, sample_width=2, input_channels=1, output_channels=2):
        sound = AudioSegment(
            # raw audio data (bytes)
            data=audiobytes,

            # 2 byte (16 bit) samples
            sample_width=sample_width,

            # 44.1 kHz frame rate
            frame_rate=original_samples,

            # stereo
            channels=input_channels
        )
        print(sound.frame_rate)
        
        ds = sound.set_frame_rate(desired_samples)
        ds = ds.set_channels(output_channels)
        print(ds.frame_rate)
        return ds
        
    def set_transcription_callback(self, callback):
        self.dg_connection.on(LiveTranscriptionEvents.Transcript, callback)
         

    def on_message(self, lwsc, result, **kwargs):
        sentence = result.channel.alternatives[0].transcript
        if len(sentence) == 0:
            return
        print(f"speaker: {sentence}")

    def on_metadata(self, lwsc, metadata, **kwargs):
        print(f"\n\n{metadata}\n\n")

    def on_speech_started(self, lwsc, speech_started, **kwargs):
        print(f"\n\n{speech_started}\n\n")

    def on_utterance_end(self, lwsc, utterance_end, **kwargs):
        print(f"\n\n{utterance_end}\n\n")

    def on_error(self, lwsc, error, **kwargs):
        print(f"\n\n{error}\n\n")

    def on_close(self, lwsc, close, **kwargs):
        print(f"\n\n{close}\n\n")





# ## create microphone
# microphone = Microphone(dg_connection.send)

# ## start microphone
# microphone.start()

# ## wait until finished
# input("Press Enter to stop recording...\n\n")

# ## Wait for the microphone to close
# microphone.finish()

# ## Indicate that we've finished
# dg_connection.finish()

# print("Finished")