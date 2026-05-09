// ===================================================================
// P5 Protocol — Message routing, state dispatch, PING heartbeat
// ===================================================================

(function() {

// ---- State routing ----
P5.updateState = function(s) {
  if (typeof s === 'string') {
    try { s = JSON.parse(s); } catch(e) { return; }
  }
  if (!s) return;

  // Route by type (server-pushed events)
  if (s.type === 'error') {
    P5.showError(s.message || s.code || 'ERROR');
    return;
  }
  if (s.type === 'hint') {
    P5.handleHint(s);
    return;
  }
  if (s.type === 'room_list') {
    P5.renderRoomList(s.rooms || []);
    return;
  }
  if (s.type === 'set_name_ok') {
    P5.onNameConfirmed(s);
    return;
  }

  // Route by game state
  P5.renderAll(s);
};

// ---- renderAll — master render dispatcher ----
P5.renderAll = function(s) {
  if (s) {
    let prevState = P5.getState();

    // Detect PASS transitions
    if (prevState && s.current_turn !== prevState.current_turn && s.last_player === prevState.last_player && s.current_turn !== prevState.last_player) {
      if (!P5._playerPassed) P5._playerPassed = {};
      P5._playerPassed[prevState.current_turn] = true;
    }

    // Detect new round
    if (s.last_player === -1) {
      P5._playerPassed = {};
      P5._lastPlayedBaseline = {};
      let plp = s.player_last_played || {};
      for (let k in plp) {
        P5._lastPlayedBaseline[k] = (plp[k]||[]).slice();
      }
    }

    // Update state in engine's closure
    P5._setState(s);
  }

  let st = P5.getState();
  if (!st) return;

  // Reset HINT cycling
  P5._hintOptions = [];
  P5._hintIndex = 0;

  // Layer switching
  var lobbyEl = document.getElementById('screen-lobby');
  var roomEl = document.getElementById('screen-room');
  var gameEl = document.getElementById('screen-game');

  if (st.state === 'WAITING') {
    // In room, waiting for game start
    if (lobbyEl) lobbyEl.style.display = 'none';
    if (roomEl) roomEl.style.display = 'block';
    if (gameEl) gameEl.style.display = 'none';
    P5.renderRoomInside(st);
  } else if (st.state === 'BIDDING' || st.state === 'BOTTOM_PICK' || st.state === 'PLAYING' || st.state === 'END') {
    // In game
    if (lobbyEl) lobbyEl.style.display = 'none';
    if (roomEl) roomEl.style.display = 'none';
    if (gameEl) gameEl.style.display = 'block';
    P5.renderGame(st);
  } else {
    // Lobby (no state)
    if (lobbyEl) lobbyEl.style.display = 'block';
    if (roomEl) roomEl.style.display = 'none';
    if (gameEl) gameEl.style.display = 'none';
  }
};

// ---- PING heartbeat ----
setInterval(function() { P5.post({action:'PING'}); }, 2000);

// ---- DOM ready ----
window.addEventListener('DOMContentLoaded', function() {
  var ph = document.getElementById('phase-text');
  if (ph) ph.textContent = 'CONNECTING...';

  // Render avatar grid in name modal
  P5.renderAvatarGrid();
});

})();
