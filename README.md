Sipcall for pilight version 6.0
=======
Sipcall add's an event action to pilight.

Requirements:
-------
- working sip-server, e.g. running on local Fritz!box
- installed sip library, according to instructions on  
    <http://binerry.de/post/29180946733/raspberry-pi-caller-and-answering-machine>  and  
    <http://www.instructables.com/id/Raspberry-Pi-water-alarm-system/?ALLSTEPS> 

Syntax:  
    
    "rules": {   
        "deurbel": {   
            "rule": "IF deurbel.state IS on THEN sipcall PHONENUMMER **620 TTS \"Someone is at the door\" ",   
            "active": 1   
        }   
    } 
          
    "settings": {  
        "sip-program": "/home/pi/sipcall/sipcall",  
        "sip-domain": "fritz.box",  
        "sip-user": "621",  
        "sip-password": "6211",  
        "sip-ttsfile": "/home/pi/sipcall/play.wav"   
    }  
    
    
PHONENUMBER: any valid phonenumber.   
TTS (TextToSpeech): short message, will be translated to voice. Notice the escaped double quotes.



