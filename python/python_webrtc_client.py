import argparse
import asyncio
import logging
import time
import pyaudio
import numpy as np
import inspect
import datetime
from audio_rms import rms, ding
from aiortc import RTCIceCandidate, RTCPeerConnection, RTCSessionDescription, RTCDataChannel
from aiortc.contrib.signaling import BYE, add_signaling_arguments
from aiortc.sdp import candidate_from_sdp, candidate_to_sdp
import json
from websockets.asyncio.server import serve
# import langchain_openai_processor
# from langchain_openai import ChatOpenAI
# import openwakeword
# from openwakeword.model import Model
from novasonic_processor import AudioStreamer, BedrockStreamManager
from pydub import AudioSegment
import scipy.io.wavfile as wav
import numpy as np
import cv2
import boto3
from polly_wrapper import PollyWrapper


FORMAT = pyaudio.paInt16
CHANNELS = 1
RATE = 16000
CHUNK = 1280*4
#audio = pyaudio.PyAudio()
Threshold = 50
TIMEOUT_LENGTH = 1 #The silent length we allow before cutting recognition
valentine_enabled = False
last_qr_data = ""
BYE = object()
# global start 
start = time.time()        
# global rec 
rec = []
# global current
current = 1
execution_start = 0
end = 0
ID = "con"
trigger = ["hey valentine","hello valentine", "valentine", "okay valentine"]
# openwakeword.utils.download_models()
# # owwModel = Model(wakeword_models=["hey_Friday!.tflite"])
# #owwModel = Model(wakeword_models=["models/hey_luna.tflite"])
# # owwModel = Model(wakeword_models=["alexa_v0.1.tflite"], inference_framework="tflite")
# owwModel = Model()
# n_models = len(owwModel.models.keys())

polly = PollyWrapper(boto3.Session().client('polly'))
stream_manager = BedrockStreamManager(model_id='amazon.nova-sonic-v1:0', region='us-east-1')
audio_streamer = AudioStreamer(stream_manager)
def yuv420_to_frame(buffer, width, height):
  """
  Converts a YUV420 buffer to an OpenCV BGR frame.

  Args:
    buffer: A byte buffer containing YUV420 data.
    width: The width of the frame.
    height: The height of the frame.

  Returns:
    An OpenCV BGR frame (numpy array).
  """
  yuv420_image = np.frombuffer(buffer, dtype=np.uint8).reshape((height * 3 // 2, width))
  bgr_frame = cv2.cvtColor(yuv420_image, cv2.COLOR_YUV2BGR_I420)
  return bgr_frame

def object_from_string(message_str):
    message = json.loads(message_str)
    
    if message["type"] in ["answer", "offer"] and message["id"] == ID:
        
        return RTCSessionDescription1(**message)
    elif message["type"] == "candidate" and message["candidate"]:
        candidate = candidate_from_sdp(message["candidate"].split(":", 1)[1])
        candidate.sdpMid = message["id"]
        candidate.sdpMLineIndex = message["label"]
        return candidate
    elif message["type"] == "bye":
        return BYE


def object_to_string(obj):    
    if isinstance(obj, RTCSessionDescription):
        message = {"sdp": obj.sdp, "type": obj.type, "id":ID}
    elif isinstance(obj, RTCIceCandidate):
        message = {
            "candidate": "candidate:" + candidate_to_sdp(obj),
            "id": obj.sdpMid,
            "label": obj.sdpMLineIndex,
            "type": "candidate",
        }
    else:
        assert obj is BYE
        message = {"type": "bye"}
    return json.dumps(message, sort_keys=True)

class WebSocketSignaling:
    def __init__(self, host, port, pc, callback):
        self._host = host
        self._port = port
        self._server = None
        self._path = None
        self.websocket = None
        self.callback = callback
        self.pc = pc


    async def connect(self):
        loop = asyncio.get_event_loop()
        connected = asyncio.Event()
        async def client_connected(websocket):
            await self.callback(self.pc, websocket)
            self.websocket = websocket
            connected.set()

        self._server = await serve(
             client_connected, host=self._host, port=self._port,  ssl=None
        )
        await self._server.serve_forever()
        await connected.wait()


    async def close(self):
            self._server.close()
            self._server = None

    async def send(self, descr):
        data = object_to_string(descr).encode("utf8")
        await self.websocket.send(data + b"\n")

# optional, for better performance
try:
    import uvloop
except ImportError:
    uvloop = None

from dataclasses import dataclass
@dataclass
class RTCSessionDescription1:
    """
    The :class:`RTCSessionDescription` dictionary describes one end of a
    connection and how it's configured.
    """

    sdp: str
    id: str
    type: str
    
    def __post_init__(self):
        if self.type not in {"offer", "pranswer", "answer", "rollback"}:
            raise ValueError(
                "'type' must be in ['offer', 'pranswer', 'answer', 'rollback'] "
                f"(got '{self.type}')"
            )
   

#Put the websocket handler here
async def consume_signaling(pc, websocket):
    # video_ch
    @pc.on("datachannel")
    def on_datachannel(channel: RTCDataChannel):
        print("channel label:", channel.label)
        if channel.label == "audio":
            if audio_streamer.audio_datachannel == None:
                audio_streamer.set_audio_datachannel(channel)
            
            print("Data Open", ding)
            
            a = ding()
            for i in range(0, len(a), 1920): #we send out the audio buffers 1920 bytes at a time                    
                chunk = (a[i:i+1920])
                channel.send(chunk)
        if channel.label == "video":
            print("Setting Video Chan")
            if audio_streamer.video_datachannel == None:
                audio_streamer.set_video_datachannel(channel)

        @pc.on("track")
        async def on_track(track):
            print("Track %s received" % track.kind)
            if track.kind == "video":
                recorder.addTrack(track)
            if track.kind == "audio":
                recorder.addTrack(track)

        @channel.on("message")
        async def on_message(message):
            global valentine_enabled
            global last_qr_data
            if channel.label=="video":
                if isinstance(message, str):
                    # if("vid_size" in message):
                    #     accept_vid = True
                    return #if this is a string, we don't handle it

                #print("Video Frame\n")
                
                width, height = 640, 480     
                np_arr = np.frombuffer(message, np.uint8)
        
                img = cv2.imdecode(np_arr, cv2.IMREAD_COLOR)
                qrDecoder = cv2.QRCodeDetector()
                 # Detect and decode the qrcode
                try:
                    data,bbox,rectifiedImage = qrDecoder.detectAndDecode(img)
                    if len(data)>0:                    
                        if data == last_qr_data:
                            return
                        print("Decoded Data : {}".format(data))
                        last_qr_data = data
                        if data == "Unlock Valentine":
                            valentine_enabled = True
                            speak("... Oh Hello there! I'm Valentine and you have my attention.")
                        if data == "Lock Valentine":
                            valentine_enabled = False
                            speak("... Cool! Talk to you later!")
                        
                        # rectifiedImage = np.uint8(rectifiedImage)
                        # cv2.imwrite("qrimage.jpg",rectifiedImage)
                    else:
                        pass
                        #print("QR Code not detected")
                        # cv2.imwrite("image.jpg",img) 
                        # cv2.imshow("Results", inputImage)
                except:
                    pass
                
            if channel.label=="audio":
                if message:
                    audio = message
                    global current
                    global end
                    global rec
                    global execution_start
                    # print("TYPE of data ")            
                    # print(type(audio))
                    # print(Threshold)
                    if isinstance(audio, str):
                        return #if this is a string, we don't handle it
                    
                    # dg_stt.dg_connection.send(audio)
                    
                    rms_val = rms(audio)
                    # print(rms_val)
                    #If audio is loud enough, set the current timeout to now and end timeout to now + TIMEOUT_LENGTH
                    #This will start the next part that stores the audio until it's quiet again
                    if rms_val > Threshold and not current <= end :
                        
                        execution_start = time.time()
                        print("Heard Something")
                        current = time.time()
                        end = time.time() + TIMEOUT_LENGTH

                    #If levels are higher than threshold add audio to record array and move the end timeout to now + TIMEOUT_LENGTH
                    #When the levels go lower than threshold, continue recording until timeout. 
                    #By doing this, we only capture relevant audio and not continously call our STT/NLP with nonsensical sounds
                    #By adding a trailing TIMEOUT_LENGTH we can capture natural pauses and make things not sound robotic
                    if current <= end: 
                        if rms_val >= Threshold: end = time.time() + TIMEOUT_LENGTH
                        current = time.time()
                        if valentine_enabled: 
                            #audio_to_process = b''.join(rec)
                            downsampled_audio = audio_streamer.resample_audio(audio, 48000, 8000,2, 2)                   
                            await audio_streamer.process_input_audio(downsampled_audio.raw_data)   

                        rec.append(audio)

                    #process audio if we have an array of non-silent audio
                    else:
                        if len(rec)>0: 
                            print("Audio Processing")
                            if valentine_enabled: 
                                pass
                                # audio_to_process = b''.join(rec)
                                # downsampled_audio = audio_streamer.resample_audio(audio_to_process, 48000, 8000,2, 2)                   
                                # await audio_streamer.process_input_audio(downsampled_audio.raw_data)   
                            else:
                                speak("...Please Scan code to unlock")

                        rec = [] #reset audio array to blank
                        
                                    
                    
                  
                else:
                    elapsed = time.time() - start
                    await signaling.send(BYE)

    while True:
        data = await websocket.recv() 
        obj = object_from_string(data)      
        print(">>",type(obj))
        
        if isinstance(obj, RTCSessionDescription1):
            await pc.setRemoteDescription(obj)

            if obj.type == "offer":
                # send answer
                await pc.setLocalDescription(await pc.createAnswer())
                print(pc.localDescription)
                await websocket.send(object_to_string(pc.localDescription) + "\n")
        elif isinstance(obj, RTCIceCandidate):
            await pc.addIceCandidate(obj)
        elif obj is BYE:
            print("Exiting")
            break
    
async def every(__seconds: float, func, *args, **kwargs):
    while True:
        func(*args, **kwargs)
        await asyncio.sleep(__seconds)

def speak(text):
    print("<<<<TEXT_TO_SPEAK>>>>")
    print(text)
    
    audioStream, visemes = polly.synthesize(text,"neural", "Joanna","pcm")
    data = audioStream.read()
    upsampled_audio =  audio_streamer.resample_audio(data, 16000, 48000)
    for i in range(0, len(upsampled_audio.raw_data), 1920): #we send out the audio buffers 1920 bytes at a time                    
        chunk = (upsampled_audio.raw_data[i:i+1920])
        #print(chunk)
        audio_streamer.audio_datachannel.send(chunk)

def debug_print(message):
    """Print only if debug mode is enabled"""
    if DEBUG:
        functionName = inspect.stack()[1].function
        if  functionName == 'time_it' or functionName == 'time_it_async':
            functionName = inspect.stack()[2].function
        print('{:%Y-%m-%d %H:%M:%S.%f}'.format(datetime.datetime.now())[:-3] + ' ' + functionName + ' ' + message)
        
def time_it(label, methodToRun):
    start_time = time.perf_counter()
    result = methodToRun()
    end_time = time.perf_counter()
    debug_print(f"Execution time for {label}: {end_time - start_time:.4f} seconds")
    return result

async def time_it_async(label, methodToRun):
    start_time = time.perf_counter()
    result = await methodToRun()
    end_time = time.perf_counter()
    debug_print(f"Execution time for {label}: {end_time - start_time:.4f} seconds")
    return result

async def main():
    """Main function to run the application."""
    global DEBUG
    DEBUG = True


    #logging.basicConfig(level=logging.DEBUG)

    pc = RTCPeerConnection()    
    signaling = WebSocketSignaling("localhost", 8000, pc, consume_signaling)    
    chan = pc.createDataChannel("video")
    

    print("Answer Here")
    await time_it_async("initialize_stream", stream_manager.initialize_stream)
    await audio_streamer.start_streaming()
    await signaling.connect()

  
    
if __name__ == "__main__":
    
    try:
        asyncio.run(main())
    except Exception as e:
        print(f"Application error: {e}")

        import traceback
        traceback.print_exc()
    
    
    
        

        
        
        
        