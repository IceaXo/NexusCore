// ===================================================================
// P5 Lobby — Name modal, room list, room inside view
// ===================================================================

(function() {

var selectedAvatar = parseInt(localStorage.getItem('nx_avatar')) || 0;
var currentRoomId = 0;
var myName = localStorage.getItem('nx_name') || '';
var myAvatar = parseInt(localStorage.getItem('nx_avatar')) || 0;
var avatarColors = ['#dc2626','#2563eb','#16a34a','#f59e0b','#8b5cf6'];

// ===================================================================
// Avatar grid
// ===================================================================
P5.renderAvatarGrid = function() {
  var grid = document.getElementById('avatar-grid');
  if (!grid) return;
  grid.innerHTML = '';
  for (var i = 0; i < 5; i++) {
    var block = document.createElement('div');
    block.className = 'avatar-block p5-font';
    if (i === selectedAvatar) block.classList.add('selected');
    block.style.backgroundColor = avatarColors[i];
    block.textContent = i;
    block.setAttribute('data-idx', i);
    block.addEventListener('click', function(e) {
      var idx = parseInt(e.currentTarget.getAttribute('data-idx'));
      selectedAvatar = idx;
      P5.renderAvatarGrid();
    });
    grid.appendChild(block);
  }
};

// Update lobby player bar
function updateLobbyBar() {
  var avatarEl = document.getElementById('lobby-avatar');
  var nameEl = document.getElementById('lobby-name');
  if (avatarEl) avatarEl.style.backgroundColor = avatarColors[myAvatar];
  if (nameEl) nameEl.textContent = myName || 'PLAYER';
}

// ===================================================================
// Confirm name
// ===================================================================
P5.confirmName = function() {
  P5.playSFX('sfx_confirm.wav');
  var input = document.getElementById('input-name');
  myName = (input && input.value.trim()) ? input.value.trim().substring(0, 12) : 'PLAYER';
  myAvatar = selectedAvatar;
  localStorage.setItem('nx_name', myName);
  localStorage.setItem('nx_avatar', myAvatar);
  // Close modal immediately — don't wait for server
  var modal = document.getElementById('name-modal');
  if (modal) modal.classList.remove('active');
  updateLobbyBar();
  P5.post({action:'SET_NAME', name: myName, avatar: myAvatar});
};

P5.onNameConfirmed = function(s) {
  var modal = document.getElementById('name-modal');
  if (modal) modal.classList.remove('active');
  // Update lobby bar from server response
  if (s && s.name) myName = s.name;
  if (s && typeof s.avatar === 'number') myAvatar = s.avatar;
  if (typeof selectedAvatar !== 'undefined') selectedAvatar = myAvatar;
  updateLobbyBar();
};

// Re-open name modal from lobby
P5.openNameModal = function() {
  P5.playSFX('sfx_confirm.wav');
  var modal = document.getElementById('name-modal');
  var input = document.getElementById('input-name');
  if (modal) modal.classList.add('active');
  if (input) input.value = myName || '';
  selectedAvatar = myAvatar;
  P5.renderAvatarGrid();
};

// ===================================================================
// Lobby buttons
// ===================================================================
P5.showSingleHint = function() {
  P5.playSFX('sfx_confirm.wav');
  var hint = document.getElementById('single-hint');
  if (hint) { hint.style.display = hint.style.display === 'none' ? 'inline' : 'none'; }
};

P5.enterRoomList = function() {
  P5.playSFX('sfx_confirm.wav');
  // Show server address modal
  var modal = document.getElementById('server-modal');
  if (modal) modal.classList.add('active');
};

P5.connectServer = function() {
  P5.playSFX('sfx_confirm.wav');
  var hostEl = document.getElementById('input-host');
  var portEl = document.getElementById('input-port');
  // Infer game server from the HTTP host that served this page.
  // If loaded via file://, fall back to hardcoded address.
  var defaultHost = window.location.hostname || '8.134.18.58';
  var defaultPort = '7777';
  var host = (hostEl && hostEl.value.trim()) ? hostEl.value.trim() : defaultHost;
  var port = parseInt(portEl && portEl.value.trim() ? portEl.value.trim() : defaultPort);
  P5.post({action:'SET_SERVER', host: host, port: port});

  var modal = document.getElementById('server-modal');
  if (modal) modal.classList.remove('active');

  // Switch to room screen
  var lobbyEl = document.getElementById('screen-lobby');
  var roomEl = document.getElementById('screen-room');
  if (lobbyEl) lobbyEl.style.display = 'none';
  if (roomEl) roomEl.style.display = 'block';
  var listView = document.getElementById('room-list-view');
  var insideView = document.getElementById('room-inside-view');
  if (listView) listView.style.display = 'block';
  if (insideView) insideView.style.display = 'none';

  // Re-send name after reconnect (old SET_NAME went to dead connection)
  setTimeout(function() {
    P5.post({action:'SET_NAME', name: myName, avatar: myAvatar});
    P5.post({action:'ROOM_LIST'});
  }, 500);
};

P5.cancelConnect = function() {
  P5.playSFX('sfx_back.wav');
  var modal = document.getElementById('server-modal');
  if (modal) modal.classList.remove('active');
};

P5.backToLobby = function() {
  P5.playSFX('sfx_back.wav');
  var lobbyEl = document.getElementById('screen-lobby');
  var roomEl = document.getElementById('screen-room');
  if (lobbyEl) lobbyEl.style.display = 'block';
  if (roomEl) roomEl.style.display = 'none';
};

// ===================================================================
// Room list
// ===================================================================
P5.renderRoomList = function(rooms) {
  var grid = document.getElementById('room-grid');
  if (!grid) return;
  grid.innerHTML = '';

  for (var i = 0; i < rooms.length; i++) {
    var r = rooms[i];
    var card = document.createElement('div');
    card.className = 'room-card';
    if (r.player_count >= 5) card.classList.add('full');
    card.innerHTML =
      '<div class="room-id p5-font">#' + r.room_id + '</div>' +
      '<div class="room-count p5-font">' + r.player_count + '/5</div>';
    card.addEventListener('click', function(rid) {
      return function() { P5.joinRoom(rid); };
    }(r.room_id));
    grid.appendChild(card);
  }
};

P5.joinRoom = function(roomId) {
  P5.playSFX('sfx_confirm.wav');
  currentRoomId = roomId;
  P5.post({action:'JOIN_ROOM', room_id: roomId});

  var listView = document.getElementById('room-list-view');
  var insideView = document.getElementById('room-inside-view');
  var title = document.getElementById('room-title');
  if (listView) listView.style.display = 'none';
  if (insideView) insideView.style.display = 'block';
  if (title) title.textContent = 'ROOM ' + roomId;
};

// ===================================================================
// Room inside view (WAITING state)
// ===================================================================
P5.renderRoomInside = function(st) {
  var title = document.getElementById('room-title');
  var me = P5.mySeat();
  var ownerSeat = st.owner_seat != null ? st.owner_seat : -1;
  var names = st.player_names || [];
  var avatars = st.player_avatars || [];
  var readyStates = st.ready_states || [];
  var online = st.player_online || [];

  if (title && currentRoomId) {
    title.textContent = 'ROOM ' + currentRoomId;
  }

  // Round selector (only for owner)
  var selWrap = document.getElementById('round-selector-wrap');
  if (selWrap) {
    selWrap.style.display = (me === ownerSeat) ? 'flex' : 'none';
    var sel = document.getElementById('round-select');
    if (sel && st.total_rounds) sel.value = st.total_rounds;
  }

  // Ready button state
  var btnReady = document.getElementById('btn-ready');
  if (btnReady && readyStates[me]) {
    btnReady.classList.add('is-ready');
  } else if (btnReady) {
    btnReady.classList.remove('is-ready');
  }

  // Render seat cards
  var seatsEl = document.getElementById('room-seats');
  if (!seatsEl) return;
  seatsEl.innerHTML = '';

  var colors = ['#dc2626','#2563eb','#16a34a','#f59e0b','#8b5cf6'];
  var occupiedCount = 0;
  for (var i = 0; i < 5; i++) {
    // AI seats have name but fd=-1 (online=false), so use name as occupancy check
    var hasPlayer = !!(names[i]);
    var isAI = hasPlayer && !online[i];
    if (hasPlayer) occupiedCount++;
    var seat = document.createElement('div');
    seat.className = 'seat-card' + (hasPlayer ? '' : ' empty') + (isAI ? ' seat-ai' : '');
    var crownHtml = (i === ownerSeat) ? '<div class="seat-crown">👑</div>' : '';
    var nameHtml = hasPlayer ? names[i] : 'EMPTY';
    var avatarColor = colors[avatars[i] || 0];
    var readyHtml = hasPlayer
      ? '<div class="seat-ready ' + (readyStates[i] ? 'ready' : 'not-ready') + ' p5-font">' + (readyStates[i] ? 'READY' : 'NOT READY') + '</div>'
      : '<div class="seat-ready not-ready p5-font">---</div>';

    var scoreText = '';
    if (st.cumulative_scores && typeof st.cumulative_scores[i] === 'number') {
      var sc = st.cumulative_scores[i];
      scoreText = '<div class="p5-font" style="font-size:14px;color:' + (sc >= 0 ? '#22c55e' : 'var(--blood)') + '">' + (sc >= 0 ? '+' : '') + sc + '</div>';
    }

    var removeBtn = '';
    if (hasPlayer && i !== me && me === ownerSeat) {
      removeBtn = '<button class="btn-seat-remove p5-font" onclick="P5.removeBot(' + i + ')">REMOVE</button>';
    }

    seat.innerHTML =
      crownHtml +
      '<div class="seat-avatar" style="background:' + (hasPlayer ? avatarColor : 'transparent') + ';color:var(--white);">' +
        (hasPlayer ? '<span>' + (avatars[i] || '?') + '</span>' : '<span style="color:rgba(255,255,255,0.2)">-</span>') +
      '</div>' +
      '<div class="seat-name p5-font">' + nameHtml + '</div>' +
      readyHtml + scoreText + removeBtn;
    seatsEl.appendChild(seat);
  }

  // Show/hide ADD_BOT button based on available seats
  var addBtn = document.getElementById('btn-addbot');
  if (addBtn) {
    addBtn.style.display = (occupiedCount < 5) ? 'inline-block' : 'none';
  }
};

// ===================================================================
// Room actions
// ===================================================================
P5.toggleReady = function() {
  P5.playSFX('sfx_confirm.wav');
  P5.post({action:'READY'});
};

P5.leaveRoom = function() {
  P5.playSFX('sfx_back.wav');
  P5.post({action:'LEAVE_ROOM'});
  currentRoomId = 0;
  // Go back to room list
  var listView = document.getElementById('room-list-view');
  var insideView = document.getElementById('room-inside-view');
  if (listView) listView.style.display = 'block';
  if (insideView) insideView.style.display = 'none';
};

P5.setRounds = function(val) {
  P5.playSFX('sfx_confirm.wav');
  P5.post({action:'SET_ROUNDS', rounds: parseInt(val)});
};

P5.addBot = function() {
  P5.playSFX('sfx_confirm.wav');
  P5.post({action:'ADD_BOT'});
};

P5.removeBot = function(seat) {
  P5.playSFX('sfx_confirm.wav');
  P5.post({action:'REMOVE_BOT', seat: seat});
};

// Init: restore stored name on page load
window.addEventListener('DOMContentLoaded', function() {
  if (myName) {
    updateLobbyBar();
    var input = document.getElementById('input-name');
    if (input) input.value = myName;
  }
});

})();
