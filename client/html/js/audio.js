// ===================================================================
// P5 Audio — BGM + SFX
// ===================================================================

(function() {

var AUDIO_BASE = 'audio/';
var S = {};           // SFX cache pool
var B = {};           // BGM cache pool (same pattern as SFX)
var bgmEl = null;     // currently playing BGM Audio element
var currentBgm = '';  // 'main' | 'game' | 'boom' | 'win' | 'lose' | ''
var prevLastPlayer = -2;
var prevState = '';
var audioReady = false;

// ---- SFX playback ----
P5.playSFX = function(name) {
  var src = AUDIO_BASE + name;
  var a = S[src];
  if (!a) {
    a = new Audio(src);
    a.volume = 0.6;
    S[src] = a;
  }
  a.currentTime = 0;
  a.play().catch(function(){});
};

// ---- BGM: cache pool, never changes .src after first load ----
P5.setBgm = function(name) {
  if (name === currentBgm) return;

  // Stop current BGM
  if (bgmEl) {
    bgmEl.onended = null;
    bgmEl.pause();
    bgmEl.currentTime = 0;
  }

  currentBgm = name;

  if (!name) return;

  // Load from cache or create
  var src = AUDIO_BASE + name + '.mp3';
  var a = B[src];
  if (!a) {
    a = new Audio(src);
    a.volume = 0.4;
    B[src] = a;
  }

  bgmEl = a;

  // onended: loop main/game, transition boom back to layer BGM
  if (name === 'boom') {
    a.onended = function() {
      P5.setBgm(_layerBgm(P5._audioLayer || 'game'));
    };
  } else if (name === 'main' || name === 'game') {
    a.onended = function() {
      if (currentBgm === name) {
        a.currentTime = 0;
        a.play().catch(function(){});
      }
    };
  }

  a.play().catch(function(){});
}

function _layerBgm(layer) {
  if (layer === 'lobby' || layer === 'room') return 'main';
  if (layer === 'game') return 'game';
  return '';
}

// ---- Inject into renderAll ----
var _origRenderAll = P5.renderAll;
P5.renderAll = function(s) {
  var st = s && s.state ? s.state : (s ? null : P5.getState() && P5.getState().state);
  var layer;

  if (st === 'WAITING')               layer = 'room';
  else if (st === 'END')              layer = 'game';  // stop BGM below
  else if (st === 'BIDDING' || st === 'BOTTOM_PICK' || st === 'PLAYING') layer = 'game';
  else                                layer = 'lobby';

  P5._audioLayer = layer;

  // Game start: entering BIDDING
  if (st === 'BIDDING' && prevState !== 'BIDDING') {
    P5.playSFX('sfx_start.wav');
  }

  // Card play detection (all players)
  var lpt = s && s.last_played_type;
  var lp = s && s.last_player;
  if (audioReady && lpt > 0 && lp !== prevLastPlayer) {
    if (lpt === 10 || lpt === 11) {
      P5.playSFX('sfx_bomb.wav');
      P5.setBgm('boom');
    } else {
      P5.playSFX('sfx_play.wav');
    }
  }
  prevLastPlayer = (typeof lp === 'number') ? lp : prevLastPlayer;
  prevState = st || '';
  audioReady = true;

  // BGM (don't overwrite boom)
  if (st === 'END') {
    P5.setBgm('');
  } else if (currentBgm !== 'boom') {
    P5.setBgm(_layerBgm(layer));
  }

  return _origRenderAll.apply(this, arguments);
};

// ---- Start BGM on page load ----
window.addEventListener('DOMContentLoaded', function() {
  P5._audioLayer = 'lobby';
  P5.setBgm('main');
});

// Periodic health check: restart BGM if it stopped unexpectedly
setInterval(function() {
  if (currentBgm && bgmEl && bgmEl.paused) {
    bgmEl.play().catch(function(){});
  }
}, 5000);

})();
