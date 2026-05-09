// ===================================================================
// P5 Lobby — Name modal, room list, room inside view
// ===================================================================

(function() {

var selectedAvatar = 0;
var currentRoomId = 0;

// ===================================================================
// Avatar grid
// ===================================================================
P5.renderAvatarGrid = function() {
  var grid = document.getElementById('avatar-grid');
  if (!grid) return;
  grid.innerHTML = '';
  var colors = ['#dc2626','#2563eb','#16a34a','#f59e0b','#8b5cf6'];
  for (var i = 0; i < 5; i++) {
    var block = document.createElement('div');
    block.className = 'avatar-block p5-font';
    if (i === selectedAvatar) block.classList.add('selected');
    block.style.backgroundColor = colors[i];
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

// ===================================================================
// Confirm name
// ===================================================================
P5.confirmName = function() {
  var input = document.getElementById('input-name');
  var name = (input && input.value.trim()) ? input.value.trim().substring(0, 12) : 'PLAYER';
  P5.post({action:'SET_NAME', name: name, avatar: selectedAvatar});
};

P5.onNameConfirmed = function(s) {
  var modal = document.getElementById('name-modal');
  if (modal) modal.classList.remove('active');
};

// ===================================================================
// Lobby buttons
// ===================================================================
P5.showSingleHint = function() {
  var hint = document.getElementById('single-hint');
  if (hint) { hint.style.display = hint.style.display === 'none' ? 'inline' : 'none'; }
};

P5.enterRoomList = function() {
  var lobbyEl = document.getElementById('screen-lobby');
  var roomEl = document.getElementById('screen-room');
  if (lobbyEl) lobbyEl.style.display = 'none';
  if (roomEl) roomEl.style.display = 'block';

  // Show room list view, hide inside view
  var listView = document.getElementById('room-list-view');
  var insideView = document.getElementById('room-inside-view');
  if (listView) listView.style.display = 'block';
  if (insideView) insideView.style.display = 'none';
};

P5.backToLobby = function() {
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
  for (var i = 0; i < 5; i++) {
    var hasPlayer = names[i] && online[i];
    var seat = document.createElement('div');
    seat.className = 'seat-card' + (hasPlayer ? '' : ' empty');
    var crownHtml = (i === ownerSeat) ? '<div class="seat-crown">👑</div>' : '';
    var nameHtml = hasPlayer ? names[i] : 'EMPTY';
    var avatarColor = colors[avatars[i] || 0];
    var readyHtml = hasPlayer
      ? '<div class="seat-ready ' + (readyStates[i] ? 'ready' : 'not-ready') + ' p5-font">' + (readyStates[i] ? 'READY' : 'NOT READY') + '</div>'
      : '<div class="seat-ready not-ready p5-font">---</div>';

    var scoreText = '';
    if (st.cumulative_scores && st.cumulative_scores[i]) {
      var sc = st.cumulative_scores[i];
      scoreText = '<div class="p5-font" style="font-size:14px;color:' + (sc >= 0 ? '#22c55e' : 'var(--blood)') + '">' + (sc >= 0 ? '+' : '') + sc + '</div>';
    }

    seat.innerHTML =
      crownHtml +
      '<div class="seat-avatar" style="background:' + (hasPlayer ? avatarColor : 'transparent') + ';color:var(--white);">' +
        (hasPlayer ? '<span>' + (avatars[i] || '?') + '</span>' : '<span style="color:rgba(255,255,255,0.2)">-</span>') +
      '</div>' +
      '<div class="seat-name p5-font">' + nameHtml + '</div>' +
      readyHtml + scoreText;
    seatsEl.appendChild(seat);
  }
};

// ===================================================================
// Room actions
// ===================================================================
P5.toggleReady = function() {
  P5.post({action:'READY'});
};

P5.leaveRoom = function() {
  P5.post({action:'LEAVE_ROOM'});
  currentRoomId = 0;
  // Go back to room list
  var listView = document.getElementById('room-list-view');
  var insideView = document.getElementById('room-inside-view');
  if (listView) listView.style.display = 'block';
  if (insideView) insideView.style.display = 'none';
};

P5.setRounds = function(val) {
  P5.post({action:'SET_ROUNDS', rounds: parseInt(val)});
};

})();
