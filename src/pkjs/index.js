// Basic Clay configuration handler
var Clay = require('pebble-clay');
var clayConfig = require('./config.json');

var clay = new Clay(clayConfig, null, { autoHandleEvents: true });

// Message key definitions
var Keys = {
  KEY_CMD_APP_READY: 0,
  KEY_CMD_PLAY: 1,
  KEY_CMD_PAUSE: 2,
  KEY_CMD_STOP: 3,
  KEY_CMD_VOL_UP: 4,
  KEY_CMD_VOL_DOWN: 5,
  KEY_CMD_PREV_TRACK: 6,
  KEY_CMD_NEXT_TRACK: 7,
  KEY_CMD_GET_STATUS: 8,
  KEY_JS_READY: 9,
  KEY_STATUS_PLAY_STATE: 10,
  KEY_STATUS_VOLUME: 11,
  KEY_STATUS_MUTE_STATE: 12,
  KEY_STATUS_ERROR_MSG: 13,
  KEY_CONFIG_IP_ADDRESS: 14,
  KEY_STATUS_TRACK_TITLE: 15,
  KEY_STATUS_ARTIST_NAME: 16,
  KEY_STATUS_ALBUM_NAME: 17
};

// Play state definitions
var PlayState = {
  STOPPED: 0,
  PLAYING: 1,
  PAUSED: 2,
  TRANSITIONING: 3,
  ERROR: 4,
  UNKNOWN: 5
};

// Store the Sonos IP address
var sonosIP = '';

// Simple XML parser function for Sonos responses
function extractValue(xml, tag) {
  var start = xml.indexOf('<' + tag + '>');
  var end = xml.indexOf('</' + tag + '>', start);
  if (start !== -1 && end !== -1) {
    return xml.substring(start + tag.length + 2, end);
  }
  return '';
}

// Basic HTTP request function
function sendRequest(url, method, headers, body, callback) {
  var xhr = new XMLHttpRequest();
  
  xhr.onload = function() {
    if (xhr.readyState === 4) {
      if (xhr.status === 200) {
        callback(null, xhr.responseText);
      } else {
        callback('HTTP Error: ' + xhr.status, null);
      }
    }
  };
  
  xhr.onerror = function() {
    callback('Network Error', null);
  };
  
  try {
    xhr.open(method, url);
    
    if (headers) {
      for (var header in headers) {
        xhr.setRequestHeader(header, headers[header]);
      }
    }
    
    xhr.send(body || null);
  } catch (e) {
    callback(e.toString(), null);
  }
}

// Simple get volume function
function getVolume(callback) {
  var body = 
    '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">' +
    '<s:Body>' +
    '<u:GetVolume xmlns:u="urn:schemas-upnp-org:service:RenderingControl:1">' +
    '<InstanceID>0</InstanceID>' +
    '<Channel>Master</Channel>' +
    '</u:GetVolume>' +
    '</s:Body>' +
    '</s:Envelope>';
  
  var headers = {
    'SOAPAction': 'urn:schemas-upnp-org:service:RenderingControl:1#GetVolume',
    'Content-Type': 'text/xml; charset="utf-8"'
  };
  
  sendRequest('http://' + sonosIP + ':1400/MediaRenderer/RenderingControl/Control', 'POST', headers, body, function(err, response) {
    if (err) {
      callback(err, 0);
    } else {
      var volume = parseInt(extractValue(response, 'CurrentVolume'));
      callback(null, isNaN(volume) ? 0 : volume);
    }
  });
}

// Simple set volume function
function setVolume(volume, callback) {
  var body = 
    '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">' +
    '<s:Body>' +
    '<u:SetVolume xmlns:u="urn:schemas-upnp-org:service:RenderingControl:1">' +
    '<InstanceID>0</InstanceID>' +
    '<Channel>Master</Channel>' +
    '<DesiredVolume>' + volume + '</DesiredVolume>' +
    '</u:SetVolume>' +
    '</s:Body>' +
    '</s:Envelope>';
  
  var headers = {
    'SOAPAction': 'urn:schemas-upnp-org:service:RenderingControl:1#SetVolume',
    'Content-Type': 'text/xml; charset="utf-8"'
  };
  
  sendRequest('http://' + sonosIP + ':1400/MediaRenderer/RenderingControl/Control', 'POST', headers, body, callback);
}

// Basic play function
function play(callback) {
  var body = 
    '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">' +
    '<s:Body>' +
    '<u:Play xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">' +
    '<InstanceID>0</InstanceID>' +
    '<Speed>1</Speed>' +
    '</u:Play>' +
    '</s:Body>' +
    '</s:Envelope>';
  
  var headers = {
    'SOAPAction': 'urn:schemas-upnp-org:service:AVTransport:1#Play',
    'Content-Type': 'text/xml; charset="utf-8"'
  };
  
  sendRequest('http://' + sonosIP + ':1400/MediaRenderer/AVTransport/Control', 'POST', headers, body, callback);
}

// Basic pause function
function pause(callback) {
  var body = 
    '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">' +
    '<s:Body>' +
    '<u:Pause xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">' +
    '<InstanceID>0</InstanceID>' +
    '</u:Pause>' +
    '</s:Body>' +
    '</s:Envelope>';
  
  var headers = {
    'SOAPAction': 'urn:schemas-upnp-org:service:AVTransport:1#Pause',
    'Content-Type': 'text/xml; charset="utf-8"'
  };
  
  sendRequest('http://' + sonosIP + ':1400/MediaRenderer/AVTransport/Control', 'POST', headers, body, callback);
}

// Next track function
function next(callback) {
  var body = 
    '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">' +
    '<s:Body>' +
    '<u:Next xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">' +
    '<InstanceID>0</InstanceID>' +
    '</u:Next>' +
    '</s:Body>' +
    '</s:Envelope>';
  
  var headers = {
    'SOAPAction': 'urn:schemas-upnp-org:service:AVTransport:1#Next',
    'Content-Type': 'text/xml; charset="utf-8"'
  };
  
  sendRequest('http://' + sonosIP + ':1400/MediaRenderer/AVTransport/Control', 'POST', headers, body, callback);
}

// Previous track function
function previous(callback) {
  var body = 
    '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">' +
    '<s:Body>' +
    '<u:Previous xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">' +
    '<InstanceID>0</InstanceID>' +
    '</u:Previous>' +
    '</s:Body>' +
    '</s:Envelope>';
  
  var headers = {
    'SOAPAction': 'urn:schemas-upnp-org:service:AVTransport:1#Previous',
    'Content-Type': 'text/xml; charset="utf-8"'
  };
  
  sendRequest('http://' + sonosIP + ':1400/MediaRenderer/AVTransport/Control', 'POST', headers, body, callback);
}

// Get transport state
function getTransportState(callback) {
  var body = 
    '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">' +
    '<s:Body>' +
    '<u:GetTransportInfo xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">' +
    '<InstanceID>0</InstanceID>' +
    '</u:GetTransportInfo>' +
    '</s:Body>' +
    '</s:Envelope>';
  
  var headers = {
    'SOAPAction': 'urn:schemas-upnp-org:service:AVTransport:1#GetTransportInfo',
    'Content-Type': 'text/xml; charset="utf-8"'
  };
  
  sendRequest('http://' + sonosIP + ':1400/MediaRenderer/AVTransport/Control', 'POST', headers, body, function(err, response) {
    if (err) {
      callback(err, 'ERROR');
    } else {
      var state = extractValue(response, 'CurrentTransportState');
      callback(null, state);
    }
  });
}

// Get current track info with extensive debugging
// Get current track info
function getTrackInfo(callback) {
  var body = 
    '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">' +
    '<s:Body>' +
    '<u:GetPositionInfo xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">' +
    '<InstanceID>0</InstanceID>' +
    '</u:GetPositionInfo>' +
    '</s:Body>' +
    '</s:Envelope>';
  
  var headers = {
    'SOAPAction': 'urn:schemas-upnp-org:service:AVTransport:1#GetPositionInfo',
    'Content-Type': 'text/xml; charset="utf-8"'
  };
  
  sendRequest('http://' + sonosIP + ':1400/MediaRenderer/AVTransport/Control', 'POST', headers, body, function(err, response) {
    if (err) {
      callback(err, { title: '', artist: '', album: '' });
      return;
    }
    
    try {
      // Extract the encoded metadata
      var metadataStart = response.indexOf('<TrackMetaData>') + 14;
      var metadataEnd = response.indexOf('</TrackMetaData>');
      
      if (metadataStart === -1 || metadataEnd === -1) {
        callback(null, { title: '', artist: '', album: '' });
        return;
      }
      
      // Get the encoded XML
      var encodedMetadata = response.substring(metadataStart, metadataEnd);
      
      // Decode HTML entities
      encodedMetadata = encodedMetadata.replace(/&lt;/g, '<')
                                       .replace(/&gt;/g, '>')
                                       .replace(/&quot;/g, '"')
                                       .replace(/&apos;/g, "'")
                                       .replace(/&amp;/g, '&');
      
      console.log('Decoded metadata:', encodedMetadata);
      
      // Now extract the actual info
      var title = '';
      var artist = '';
      var album = '';
      
      // Find title
      var titleStart = encodedMetadata.indexOf('<dc:title>');
      if (titleStart !== -1) {
        var titleEnd = encodedMetadata.indexOf('</dc:title>', titleStart);
        if (titleEnd !== -1) {
          title = encodedMetadata.substring(titleStart + 10, titleEnd);
          console.log('Extracted title:', title);
        }
      }
      
      // Find artist
      var artistStart = encodedMetadata.indexOf('<dc:creator>');
      if (artistStart !== -1) {
        var artistEnd = encodedMetadata.indexOf('</dc:creator>', artistStart);
        if (artistEnd !== -1) {
          artist = encodedMetadata.substring(artistStart + 12, artistEnd);
          console.log('Extracted artist:', artist);
        }
      }
      
      // Find album
      var albumStart = encodedMetadata.indexOf('<upnp:album>');
      if (albumStart !== -1) {
        var albumEnd = encodedMetadata.indexOf('</upnp:album>', albumStart);
        if (albumEnd !== -1) {
          album = encodedMetadata.substring(albumStart + 12, albumEnd);
          console.log('Extracted album:', album);
        }
      }
      
      callback(null, {
        title: title || 'Unknown Title',
        artist: artist || 'Unknown Artist',
        album: album || 'Unknown Album'
      });
    } catch (e) {
      console.log('Error parsing track info:', e);
      callback(null, { title: '', artist: '', album: '' });
    }
  });
}


// Alternate method to get track info using GetMediaInfo
function getMediaInfo(callback) {
  var body = 
    '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">' +
    '<s:Body>' +
    '<u:GetMediaInfo xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">' +
    '<InstanceID>0</InstanceID>' +
    '</u:GetMediaInfo>' +
    '</s:Body>' +
    '</s:Envelope>';
  
  var headers = {
    'SOAPAction': 'urn:schemas-upnp-org:service:AVTransport:1#GetMediaInfo',
    'Content-Type': 'text/xml; charset="utf-8"'
  };
  
  console.log('Sending GetMediaInfo request to ' + sonosIP);
  
  sendRequest('http://' + sonosIP + ':1400/MediaRenderer/AVTransport/Control', 'POST', headers, body, function(err, response) {
    if (err) {
      console.log('Error getting media info: ' + err);
      callback(err, null);
      return;
    }
    
    console.log('GetMediaInfo response received:');
    console.log('------ MEDIA INFO START ------');
    console.log(response);
    console.log('------ MEDIA INFO END ------');
    
    callback(null, response);
  });
}

// Get full status and send to watch
// Get full status and send to watch
function getStatus() {
  // Try to get IP from local storage if not set yet
  if (!sonosIP) {
    sonosIP = localStorage.getItem('KEY_CONFIG_IP_ADDRESS');
    console.log('Looking up IP in localStorage: ' + sonosIP);
  }
  
  // Check if we have an IP
  if (!sonosIP) {
    console.log('No IP address configured');
    var msg = {};
    msg[Keys.KEY_STATUS_ERROR_MSG] = "No IP configured";
    Pebble.sendAppMessage(msg);
    return;
  }
  
  console.log('Using Sonos IP: ' + sonosIP);
  
  getTransportState(function(err, state) {
    if (err) {
      console.log('Error getting transport state: ' + err);
      var msg = {};
      msg[Keys.KEY_STATUS_ERROR_MSG] = "Connection Error";
      Pebble.sendAppMessage(msg);
      return;
    }
    
    var playState;
    switch (state) {
      case 'PLAYING': playState = PlayState.PLAYING; break;
      case 'PAUSED_PLAYBACK': playState = PlayState.PAUSED; break;
      case 'STOPPED': playState = PlayState.STOPPED; break;
      case 'TRANSITIONING': playState = PlayState.TRANSITIONING; break;
      default: playState = PlayState.UNKNOWN;
    }
    
    console.log('Current play state: ' + state + ' (' + playState + ')');
    
    getVolume(function(volErr, volume) {
      var msg = {};
      msg[Keys.KEY_STATUS_PLAY_STATE] = playState;
      msg[Keys.KEY_STATUS_VOLUME] = volume || 0;
      
      console.log('Current volume: ' + volume);
      
      if (playState === PlayState.PLAYING || playState === PlayState.PAUSED) {
        getTrackInfo(function(trackErr, track) {
          msg[Keys.KEY_STATUS_TRACK_TITLE] = track.title;
          msg[Keys.KEY_STATUS_ARTIST_NAME] = track.artist;
          msg[Keys.KEY_STATUS_ALBUM_NAME] = track.album;
          
          console.log('Sending status with track info:', track);
          Pebble.sendAppMessage(msg, 
            function() { console.log('Status sent successfully'); },
            function(err) { console.log('Failed to send status: ' + JSON.stringify(err)); }
          );
        });
      } else {
        Pebble.sendAppMessage(msg);
      }
    });
  });
}


// App ready event
Pebble.addEventListener('ready', function() {
  console.log('PebbleKit JS ready!');
  
  // Try to load IP address from localStorage
  sonosIP = localStorage.getItem('KEY_CONFIG_IP_ADDRESS');
  console.log('Stored IP: ' + sonosIP);
  
  // Notify the watch that JS is ready
  Pebble.sendAppMessage({
    [Keys.KEY_JS_READY]: 1
  }, 
  function() { console.log('Ready message sent successfully'); },
  function(err) { console.log('Failed to send ready message: ' + JSON.stringify(err)); }
  );
  
  // Get status if we have an IP
  if (sonosIP) {  
    getStatus();
  }
});

// Handle messages from the watch
Pebble.addEventListener('appmessage', function(e) {
  var cmd = e.payload;
  console.log('Received message: ' + JSON.stringify(cmd));
  
  if (cmd[Keys.KEY_CMD_APP_READY]) {
    console.log('Watch app is ready');
    Pebble.sendAppMessage({
      [Keys.KEY_JS_READY]: 1
    });
  } else if (cmd[Keys.KEY_CMD_GET_STATUS]) {
    console.log('Get status');
    getStatus();
  } else if (cmd[Keys.KEY_CMD_PLAY]) {
    console.log('Play');
    
    // First, immediately send a message indicating we're transitioning
    Pebble.sendAppMessage({
      [Keys.KEY_STATUS_PLAY_STATE]: PlayState.TRANSITIONING
    });
    
    play(function(err) {
      if (err) {
        console.log('Error playing: ' + err);
        Pebble.sendAppMessage({
          [Keys.KEY_STATUS_ERROR_MSG]: "Play Error"
        });
      }
      
      // Poll status more frequently after play
      getStatus();
      
      // Set up a series of status checks at decreasing intervals
      setTimeout(function() { 
        getStatus(); 
        setTimeout(function() { 
          getStatus(); 
          setTimeout(getStatus, 1000);
        }, 1000);
      }, 500);
    });
  

  

  } else if (cmd[Keys.KEY_CMD_PAUSE]) {
    console.log('Pause');
    pause(function(err) {
      if (err) console.log('Error pausing: ' + err);
      getStatus();
    });
  } else if (cmd[Keys.KEY_CMD_NEXT_TRACK]) {
    console.log('Next track');
    next(function(err) {
      if (err) console.log('Error next track: ' + err);
      getStatus();
    });
  } else if (cmd[Keys.KEY_CMD_PREV_TRACK]) {
    console.log('Previous track');
    previous(function(err) {
      if (err) console.log('Error previous track: ' + err);
      getStatus();
    });
  } else if (cmd[Keys.KEY_CMD_VOL_UP]) {
    console.log('Volume up');
    getVolume(function(err, volume) {
      if (err) {
        console.log('Error getting volume: ' + err);
        return;
      }
      
      volume = Math.min(100, volume + 5);
      console.log('Setting volume to: ' + volume);
      setVolume(volume, function(setErr) {
        if (setErr) {
          console.log('Error setting volume: ' + setErr);
        } else {
          Pebble.sendAppMessage({
            [Keys.KEY_STATUS_VOLUME]: volume
          });
        }
      });
    });
  } else if (cmd[Keys.KEY_CMD_VOL_DOWN]) {
    console.log('Volume down');
    getVolume(function(err, volume) {
      if (err) {
        console.log('Error getting volume: ' + err);
        return;
      }
      
      volume = Math.max(0, volume - 5);
      console.log('Setting volume to: ' + volume);
      setVolume(volume, function(setErr) {
        if (setErr) {
          console.log('Error setting volume: ' + setErr);
        } else {
          Pebble.sendAppMessage({
            [Keys.KEY_STATUS_VOLUME]: volume
          });
        }
      });
    });
  }
});

// Handle configuration
Pebble.addEventListener('showConfiguration', function() {
  var url = clay.generateUrl();
  console.log('Opening config page: ' + url);
  Pebble.openURL(url);
});

Pebble.addEventListener('webviewclosed', function(e) {
  console.log('Configuration page closed');
  if (e && e.response) {
    console.log('Configuration response: ' + e.response);
    var settings = clay.getSettings(e.response);
    console.log('Parsed settings: ' + JSON.stringify(settings));
    
    if (settings.KEY_CONFIG_IP_ADDRESS) {
      sonosIP = settings.KEY_CONFIG_IP_ADDRESS;
      console.log('Setting Sonos IP: ' + sonosIP);
      localStorage.setItem('KEY_CONFIG_IP_ADDRESS', sonosIP);
      
      // Notify Pebble app about the new IP
      console.log('Sending config data to Pebble');
      Pebble.sendAppMessage({
        [Keys.KEY_CONFIG_IP_ADDRESS]: sonosIP
      }, 
      function() { console.log('Sent config data to Pebble'); },
      function(err) { console.log('Failed to send config data: ' + JSON.stringify(err)); }
      );
      
      getStatus();
    }
  }
});
