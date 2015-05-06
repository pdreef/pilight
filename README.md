Sipcall for pilight version 6.0
=======
Sipcall add's an event action to pilight.

Requirements:
-------
- working sip-server, e.g. running on local Fritz!box
- PJSUA API (<http://www.pjsip.org>)  
- eSpeak (<http://espeak.sourceforge.net>)  

Follow instructions on <http://binerry.de/post/29180946733/raspberry-pi-caller-and-answering-machine> but use <https://github.com/pdreef/sipcall> instead (put the code in e.g. /home/pi/sipcall and have your setting 'sip-program' point to this file)  
You can modify sipcall.c to set voice generating options (eSpeak or Google Translate, language)  


Syntax:
-------  
    
    "rules": {   
        "deurbel1": {   
            "rule": "IF deurbel.state IS on THEN switch DEVICE deurbel TO off AND sipcall PHONENUMMER **9 TTS \"Someone is at the door\" ",   
            "active": 1   
        },  
        "deurbel12": {
            "rule": "IF deurbel.state IS on THEN switch DEVICE deurbel TO off AND sipcall PHONENUMBER **9 TTSFILE doorbell.wav",  
            "active": 1  
        },  
    } 
          
    "settings": {  
        "sip-program": "/home/pi/sipcall/sipcall",  
        "sip-domain": "fritz.box",  
        "sip-user": "621",  
        "sip-password": "6211",  
        "sip-ttspath": "/home/pi/sipcall"   
    }  
    


_Action parameters:_  
-
PHONENUMBER: A valid phonenumber. On Fritz!box **9 calls all connected phones   
TTS: (TextToSpeech): short message, will be translated to voice. Notice the escaped double quotes.  
TTSFILE: File (default play.wav) to use for playback  

Use TTS or TTSFile, not both.

