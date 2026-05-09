// ===================================================================
// P5 Game — BIDDING, BOTTOM_PICK, PLAYING, END, settlement
// ===================================================================

(function() {

// ---- HINT cycling state ----
P5._hintOptions = [];
P5._hintIndex = 0;

// ---- BOTTOM_PICK state ----
var bottomPickSelected = [];
var maxBottomPicks = 2;
P5._isAnimatingBottomPick = false;
P5._pendingState = null;
var bottomPickIndicesSaved = null;
var bottomPickLandlordSaved = -1;

// ===================================================================
// renderGame — master game render
// ===================================================================
P5.renderGame = function(st) {
  var me = P5.mySeat();

  // Phase text
  var phaseMap = {WAITING:'WAITING',BIDDING:'BIDDING',BOTTOM_PICK:'BOTTOM PICK',PLAYING:'ENGAGED!!',END:'FIN'};
  var phaseEl = document.getElementById('phase-text');
  if (phaseEl) phaseEl.textContent = phaseMap[st.state] || st.state;

  // Multiplier
  var mul = Math.max(1, st.multiplier||1);
  var mulEl = document.getElementById('multiplier-value');
  if (mulEl) mulEl.textContent = 'X ' + mul;

  // ================================================================
  // Action bar — only during PLAYING when it's my turn
  // ================================================================
  var bar = document.getElementById('action-bar');
  var wi = document.getElementById('waiting-indicator');
  var isMyTurn = st.state === 'PLAYING' && st.current_turn === me;
  if (bar) bar.style.display = isMyTurn ? 'flex' : 'none';
  if (wi) wi.style.display = (st.state === 'PLAYING' && !isMyTurn) ? 'block' : 'none';

  // ================================================================
  // Bidding overlay — during BIDDING
  // ================================================================
  var bo = document.getElementById('bidding-overlay');
  var bb = document.getElementById('bidding-buttons');
  var bl = document.getElementById('bidding-label');
  if (st.state === 'BIDDING') {
    if (bo) bo.classList.add('active');
    var isMyBid = String(st.current_bidder) === String(me);
    if (bb) bb.style.display = isMyBid ? 'flex' : 'none';
    if (bl) {
      var bidderName = (st.player_names && st.player_names[st.current_bidder]) || P5.PLAYER_NAMES[st.current_bidder] || 'OPPONENT';
      bl.textContent = isMyBid
        ? 'CALL THE SHOT (YOUR TURN)'
        : 'WAITING FOR ' + bidderName + ' TO BID...';
    }
  } else {
    if (bo) bo.classList.remove('active');
  }

  // ================================================================
  // BOTTOM_PICK area
  // ================================================================
  var bpArea = document.getElementById('bottom-pick-area');
  if (P5._isAnimatingBottomPick) {
    /* animation owns bpArea */
  } else if (st.state === 'BOTTOM_PICK') {
    if (bpArea) bpArea.style.display = 'flex';
    if (st.bottom_pick_indices) bottomPickIndicesSaved = st.bottom_pick_indices.slice();
    if (typeof st.bottom_pick_landlord === 'number') bottomPickLandlordSaved = st.bottom_pick_landlord;
    P5.renderBottomPickCards(st);
    var confirmBtn = document.getElementById('btn-confirm-pick');
    if (confirmBtn) confirmBtn.style.display = st.is_picking ? 'block' : 'none';
  } else {
    if (bpArea) bpArea.style.display = 'none';
    bottomPickSelected = [];
  }

  // ================================================================
  // Top mini bottom cards bar (during PLAYING/END)
  // ================================================================
  var miniBar = document.getElementById('bottom-mini-bar');
  if (P5._isAnimatingBottomPick) {
    if (miniBar) miniBar.style.display = 'none';
  } else if (st.state === 'PLAYING' || st.state === 'END') {
    if (miniBar) miniBar.style.display = 'flex';
    P5.renderMiniBottomCards(st);
  } else {
    if (miniBar) miniBar.style.display = 'none';
  }

  // ================================================================
  // Result overlay — END state only; force-hide otherwise
  // ================================================================
  if (st.state === 'END') {
    P5.renderEndScreen(st, me);
  } else {
    var ov = document.getElementById('result-overlay');
    if (ov) ov.classList.remove('active');
  }

  // Render players, center, hand
  P5.renderPlayers();
  P5.renderCenterPlayed();
  P5.renderHand();
};

// ===================================================================
// BOTTOM_PICK: render 4 card backs
// ===================================================================
P5.renderBottomPickCards = function(st) {
  if (P5._isAnimatingBottomPick) return;
  var ctr = document.getElementById('bottom-pick-cards');
  if (!ctr) return;
  ctr.innerHTML = '';

  var count = st.bottom_cards_count || 4;
  for (var i = 0; i < count; i++) {
    var card = document.createElement('div');
    card.className = 'pick-card-back';
    card.setAttribute('data-idx', i);

    // Check if already picked (server state)
    if (st.bottom_pick_indices) {
      if (st.bottom_pick_indices[0] === i || st.bottom_pick_indices[1] === i) {
        card.classList.add('selected');
      }
    } else if (bottomPickSelected.indexOf(i) >= 0) {
      card.classList.add('selected');
    }

    card.addEventListener('click', function(e) {
      if (!st.is_picking) return;
      var idx = parseInt(e.currentTarget.getAttribute('data-idx'));
      var selIdx = bottomPickSelected.indexOf(idx);
      if (selIdx >= 0) {
        bottomPickSelected.splice(selIdx, 1);
        e.currentTarget.classList.remove('selected');
      } else if (bottomPickSelected.length < maxBottomPicks) {
        bottomPickSelected.push(idx);
        e.currentTarget.classList.add('selected');
      }
    });

    ctr.appendChild(card);
  }
};

P5.confirmBottomPick = function() {
  if (P5._isAnimatingBottomPick) return;
  if (bottomPickSelected.length !== 2) return;
  bottomPickIndicesSaved = bottomPickSelected.slice();
  P5.post({action:'PICK_BOTTOM', indices: bottomPickSelected.slice()});
  bottomPickSelected = [];
};

// ===================================================================
// BOTTOM_PICK → PLAYING reveal + fly animation
// ===================================================================
function getPlayerCenter(seat) {
  var me = P5.mySeat();
  var posIdx = (seat - me - 1 + 10) % 5;
  var pos = P5.PLAYER_POS[posIdx];
  if (!pos) return {x: 100, y: 100};
  return {x: pos.x + 32, y: pos.y + 32};
}

function buildFlipCard(cardVal) {
  var isR = P5.isRed(cardVal);
  var isJ = P5.isJoker(cardVal);

  var wrapper = document.createElement('div');
  wrapper.className = 'flip-card-wrapper';

  var container = document.createElement('div');
  container.className = 'flip-container';

  var flipper = document.createElement('div');
  flipper.className = 'flipper';

  var front = document.createElement('div');
  front.className = 'flip-front';

  var back = document.createElement('div');
  back.className = 'flip-back';
  if (isR) back.classList.add('red');

  if (isJ) {
    back.innerHTML = '<span class="flip-rank">' + P5.rankStr(cardVal) + '</span><span class="flip-suit">★</span>';
  } else {
    back.innerHTML = '<span class="flip-rank">' + P5.rankStr(cardVal) + '</span><span class="flip-suit">' + P5.suitStr(cardVal) + '</span>';
  }

  flipper.appendChild(front);
  flipper.appendChild(back);
  container.appendChild(flipper);
  wrapper.appendChild(container);
  return wrapper;
}

P5.playBottomPickAnimation = async function(st) {
  var bpArea = document.getElementById('bottom-pick-area');
  var bpCards = document.getElementById('bottom-pick-cards');
  var confirmBtn = document.getElementById('btn-confirm-pick');
  var title = bpArea ? bpArea.querySelector('.bottom-pick-title') : null;

  var landlords = st.landlords || [];
  var bottomCards = st.bottom_cards || [];
  var names = st.player_names || [];

  if (!bpArea || landlords.length < 2 || bottomCards.length < 4) {
    P5._isAnimatingBottomPick = false;
    P5.renderGame(st);
    return;
  }

  // First picker = the landlord who actually picked (saved from BOTTOM_PICK state)
  var firstPicker = (bottomPickLandlordSaved >= 0) ? bottomPickLandlordSaved : landlords[0];
  var secondPicker = -1;
  for (var li = 0; li < landlords.length; li++) {
    if (landlords[li] !== firstPicker) { secondPicker = landlords[li]; break; }
  }
  if (secondPicker === -1) secondPicker = landlords[1];

  // Build position mapping: selected indices first, then non-selected
  var selected = bottomPickIndicesSaved || [0, 1];
  var notSelected = [];
  for (var p = 0; p < 4; p++) {
    if (selected[0] !== p && selected[1] !== p) notSelected.push(p);
  }
  var posOrder = selected.concat(notSelected);   // e.g. [0,3,1,2]
  var baseOffsets = [-186, -62, 62, 186];         // pixel offsets for each original position

  // Stage: clear existing pick cards, keep area visible, hide confirm
  if (bpCards) bpCards.innerHTML = '';
  if (confirmBtn) confirmBtn.style.display = 'none';
  if (title) title.textContent = 'REVEALING...';
  bpArea.style.display = 'flex';

  // Create 4 flip wrappers at original display positions
  var wrappers = [];
  var flippers = [];
  var pickedWrappers = [];
  var remainingWrappers = [];

  for (var i = 0; i < 4; i++) {
    var origPos = posOrder[i];                    // which screen position this card belongs at
    var wrapper = buildFlipCard(bottomCards[i]);
    wrapper.style.left = 'calc(50% + ' + baseOffsets[origPos] + 'px - 60px)';
    wrapper.style.top = 'calc(50% - 92px)';
    wrapper.setAttribute('data-orig-pos', origPos);
    bpArea.appendChild(wrapper);
    wrappers.push(wrapper);
    flippers.push(wrapper.querySelector('.flipper'));

    if (i < 2) {
      wrapper.setAttribute('data-picked', '1');
      pickedWrappers.push(wrapper);
    } else {
      wrapper.setAttribute('data-picked', '0');
      remainingWrappers.push(wrapper);
    }
  }

  await new Promise(function(r) { setTimeout(r, 400); });

  // --- Step 2: flip first picker's selected cards ---
  for (var j = 0; j < pickedWrappers.length; j++) {
    pickedWrappers[j].querySelector('.flipper').classList.add('flipped');
  }
  await new Promise(function(r) { setTimeout(r, 800); });

  // --- Step 3: fly selected cards to first picker ---
  var tA = getPlayerCenter(firstPicker);
  var nameA = names[firstPicker] || P5.PLAYER_NAMES[firstPicker] || ('P' + firstPicker);
  if (title) title.textContent = nameA + ' PICKS';
  for (var k = 0; k < pickedWrappers.length; k++) {
    pickedWrappers[k].style.left = tA.x + 'px';
    pickedWrappers[k].style.top = tA.y + 'px';
    pickedWrappers[k].classList.add('fly-out');
  }
  await new Promise(function(r) { setTimeout(r, 900); });

  // Remove flown cards
  for (var m = 0; m < pickedWrappers.length; m++) {
    pickedWrappers[m].remove();
  }

  // --- Step 4: flip second picker's cards ---
  for (var n = 0; n < remainingWrappers.length; n++) {
    remainingWrappers[n].querySelector('.flipper').classList.add('flipped');
  }
  await new Promise(function(r) { setTimeout(r, 800); });

  // --- Step 5: fly remaining cards to second picker ---
  var tB = getPlayerCenter(secondPicker);
  var nameB = names[secondPicker] || P5.PLAYER_NAMES[secondPicker] || ('P' + secondPicker);
  if (title) title.textContent = nameB + ' PICKS';
  for (var q = 0; q < remainingWrappers.length; q++) {
    remainingWrappers[q].style.left = tB.x + 'px';
    remainingWrappers[q].style.top = tB.y + 'px';
    remainingWrappers[q].classList.add('fly-out');
  }
  await new Promise(function(r) { setTimeout(r, 900); });

  for (var r = 0; r < remainingWrappers.length; r++) {
    remainingWrappers[r].remove();
  }

  // --- Step 6: cleanup & resume normal render ---
  P5._isAnimatingBottomPick = false;
  bpArea.style.display = 'none';

  var renderSt = P5._pendingState || st;
  P5._pendingState = null;
  P5.renderGame(renderSt);
};

// ===================================================================
// Top mini bottom cards bar
// ===================================================================
P5.renderMiniBottomCards = function(st) {
  var cardsA = document.getElementById('mini-cards-a');
  var cardsB = document.getElementById('mini-cards-b');
  var labelA = document.getElementById('mini-label-a');
  var labelB = document.getElementById('mini-label-b');

  var landlords = st.landlords || [];
  var names = st.player_names || [];
  var bottomCards = st.bottom_cards || [];

  if (landlords.length >= 2 && bottomCards.length >= 4) {
    var llA = (bottomPickLandlordSaved >= 0) ? bottomPickLandlordSaved : landlords[0];
    var llB = -1;
    for (var li = 0; li < landlords.length; li++) {
      if (landlords[li] !== llA) { llB = landlords[li]; break; }
    }
    if (llB === -1) llB = landlords[1];
    if (labelA) labelA.textContent = names[llA] || P5.PLAYER_NAMES[llA] || ('P' + llA);
    if (labelB) labelB.textContent = names[llB] || P5.PLAYER_NAMES[llB] || ('P' + llB);

    if (cardsA) {
      cardsA.innerHTML = '';
      for (var i = 0; i < 2; i++) {
        var c = bottomCards[i];
        var el = document.createElement('div');
        el.className = 'mini-card';
        if (P5.isRed(c)) el.classList.add('red');
        el.innerHTML = '<span>' + P5.rankStr(c) + '</span><span>' + P5.suitStr(c) + '</span>';
        cardsA.appendChild(el);
      }
    }
    if (cardsB) {
      cardsB.innerHTML = '';
      for (var i = 2; i < 4; i++) {
        var c = bottomCards[i];
        var el = document.createElement('div');
        el.className = 'mini-card';
        if (P5.isRed(c)) el.classList.add('red');
        el.innerHTML = '<span>' + P5.rankStr(c) + '</span><span>' + P5.suitStr(c) + '</span>';
        cardsB.appendChild(el);
      }
    }
  }
};

// ===================================================================
// END screen: settlement / final results
// ===================================================================
P5.renderEndScreen = function(st, me) {
  var ov = document.getElementById('result-overlay');
  if (!ov) return;
  ov.classList.add('active');

  var title = document.getElementById('result-title');
  var sub = document.getElementById('result-sub');
  var scoresEl = document.getElementById('result-scores');
  var btnContinue = document.getElementById('btn-continue');

  // Victory/Defeat
  var imLandlord = (st.landlords||[]).includes(me);
  var winner = st.winner;
  var winnerIsLandlord = (st.landlords||[]).includes(winner);
  var win = imLandlord === winnerIsLandlord;
  var isLastRound = st.current_round >= st.total_rounds;

  if (title) {
    if (isLastRound) {
      title.textContent = 'FINAL RESULTS';
      title.style.color = 'var(--white)';
    } else {
      title.textContent = win ? 'VICTORY' : 'DEFEAT';
      title.style.color = win ? 'var(--white)' : 'var(--blood)';
    }
  }

  if (sub) {
    sub.textContent = isLastRound
      ? (win ? 'YOU WIN THE MATCH' : 'YOU LOSE THE MATCH')
      : ('ROUND ' + st.current_round + '/' + st.total_rounds);
  }

  // Score table
  if (scoresEl) {
    scoresEl.innerHTML = '';
    var roundScores = st.round_scores || [];
    var cumScores = st.cumulative_scores || [];
    var names = st.player_names || [];
    var landlords = st.landlords || [];

    if (isLastRound) {
      // Final leaderboard: sort by cumulative_scores descending
      var sorted = [];
      for (var i = 0; i < 5; i++) {
        var name = names[i] || P5.PLAYER_NAMES[i];
        var isLL = landlords.indexOf(i) >= 0;
        sorted.push({idx: i, score: cumScores[i] || 0, name: name, isLandlord: isLL});
      }
      sorted.sort(function(a, b) { return b.score - a.score; });

      // Header row
      var hdr = document.createElement('div');
      hdr.className = 'score-row p5-font';
      hdr.style.cssText = 'border-color:rgba(255,255,255,0.3);margin-bottom:4px;';
      hdr.innerHTML = '<span class="score-name" style="font-size:12px;letter-spacing:2px;">RANK  PLAYER</span>' +
                      '<span class="score-val" style="font-size:12px;">SCORE</span>';
      scoresEl.appendChild(hdr);

      for (var j = 0; j < sorted.length; j++) {
        var row = document.createElement('div');
        row.className = 'score-row p5-font';
        if (j === 0) row.classList.add('gold');
        var valClass = sorted[j].score >= 0 ? 'positive' : 'negative';
        var teamTag = sorted[j].isLandlord ? ' [地主]' : ' [农民]';
        row.innerHTML = '<span class="score-name">#' + (j+1) + ' ' + sorted[j].name +
                        '<span style="font-size:11px;opacity:0.6;">' + teamTag + '</span></span>' +
                        '<span class="score-val ' + valClass + '">' + (sorted[j].score >= 0 ? '+' : '') + sorted[j].score + '</span>';
        scoresEl.appendChild(row);
      }

      // Team totals
      var llTotal = 0, fmTotal = 0;
      for (var i = 0; i < 5; i++) {
        var sc = cumScores[i] || 0;
        if (landlords.indexOf(i) >= 0) llTotal += sc; else fmTotal += sc;
      }
      var teamRow = document.createElement('div');
      teamRow.className = 'score-row p5-font';
      teamRow.style.cssText = 'border-color:rgba(255,255,255,0.3);margin-top:8px;';
      teamRow.innerHTML = '<span class="score-name">TEAM TOTALS</span>' +
                          '<span><span style="color:#22c55e;">地主 +' + Math.max(llTotal,0) + '</span>  ' +
                          '<span style="color:var(--blood);">农民 +' + Math.max(fmTotal,0) + '</span></span>';
      scoresEl.appendChild(teamRow);
    } else {
      // Mid-round: round_scores + cumulative_scores + team tags
      // Header
      var hdr = document.createElement('div');
      hdr.className = 'score-row p5-font';
      hdr.style.cssText = 'border-color:rgba(255,255,255,0.3);margin-bottom:4px;';
      hdr.innerHTML = '<span class="score-name" style="font-size:12px;letter-spacing:2px;">PLAYER</span>' +
                      '<span><span class="score-val" style="font-size:12px;margin-right:32px;">ROUND</span>' +
                      '<span class="score-val" style="font-size:12px;">TOTAL</span></span>';
      scoresEl.appendChild(hdr);

      for (var i = 0; i < 5; i++) {
        var row = document.createElement('div');
        row.className = 'score-row p5-font';
        var rs = roundScores[i] || 0;
        var cs = cumScores[i] || 0;
        var rClass = rs >= 0 ? 'positive' : 'negative';
        var cClass = cs >= 0 ? 'positive' : 'negative';
        var displayName = names[i] || P5.PLAYER_NAMES[i];
        var isLL = landlords.indexOf(i) >= 0;
        var teamTag = isLL ? ' [地主]' : ' [农民]';
        row.innerHTML = '<span class="score-name">' + displayName +
                        '<span style="font-size:11px;opacity:0.6;">' + teamTag + '</span></span>' +
                        '<span><span class="score-val ' + rClass + '" style="margin-right:32px;">' + (rs >= 0 ? '+' : '') + rs + '</span>' +
                        '<span class="score-val ' + cClass + '">' + (cs >= 0 ? '+' : '') + cs + '</span></span>';
        scoresEl.appendChild(row);
      }

      // Team totals
      var llRound = 0, fmRound = 0;
      for (var i = 0; i < 5; i++) {
        var sc = roundScores[i] || 0;
        if (landlords.indexOf(i) >= 0) llRound += sc; else fmRound += sc;
      }
      var teamRow = document.createElement('div');
      teamRow.className = 'score-row p5-font';
      teamRow.style.cssText = 'border-color:rgba(255,255,255,0.3);margin-top:8px;';
      teamRow.innerHTML = '<span class="score-name">TEAM TOTALS</span>' +
                          '<span><span style="color:#22c55e;">地主 ' + (llRound >= 0 ? '+' : '') + llRound + '</span>  ' +
                          '<span style="color:var(--blood);">农民 ' + (fmRound >= 0 ? '+' : '') + fmRound + '</span></span>';
      scoresEl.appendChild(teamRow);
    }
  }

  if (btnContinue) {
    btnContinue.style.display = 'block';
    btnContinue.textContent = isLastRound ? 'BACK TO ROOM' : 'CONTINUE';
  }
};

P5.clickContinue = function() {
  P5.post({action:'CONTINUE'});
  var ov = document.getElementById('result-overlay');
  if (ov) ov.classList.remove('active');
};

// ===================================================================
// Game actions
// ===================================================================
P5.pass = function() {
  P5.selectedSet.clear();
  P5.renderHand();
  P5.post({action:'PASS'});
};

P5.hint = function() {
  if (P5._hintOptions.length > 0) {
    P5._hintIndex = (P5._hintIndex + 1) % P5._hintOptions.length;
    P5.selectHintOption();
  } else {
    P5.post({action:'HINT'});
  }
};

P5.play = function() {
  var idxs = Array.from(P5.selectedSet).sort(function(a,b){return a-b;});
  if (idxs.length === 0) return;
  var st = P5.getState();
  P5.post({action:'PLAY', cards: idxs.map(function(i){return st.my_cards[i];})});
  P5.selectedSet.clear();
  P5.renderHand();
};

P5.selectHintOption = function() {
  var option = P5._hintOptions[P5._hintIndex];
  if (!option) return;
  P5.selectedSet.clear();
  var st = P5.getState();
  var handCopy = st.my_cards.slice();
  for (var i = 0; i < option.length; i++) {
    var val = option[i];
    var idx = handCopy.indexOf(val);
    if (idx >= 0) {
      P5.selectedSet.add(idx);
      handCopy[idx] = -1;
    }
  }
  P5.renderHand();
};

P5.handleHint = function(resp) {
  P5._hintOptions = resp.options || [];
  P5._hintIndex = 0;
  if (P5._hintOptions.length > 0) {
    P5.selectHintOption();
  }
};

// ---- Speed toggle (1x → 2x → 3x → 1x) ----
P5._speedLevel = 1;
P5.toggleSpeed = function() {
  P5._speedLevel = (P5._speedLevel % 3) + 1;  // 1→2→3→1
  var btn = document.getElementById('btn-speed');
  if (btn) {
    btn.textContent = P5._speedLevel + 'x';
    btn.classList.remove('speed-2x', 'speed-3x');
    if (P5._speedLevel >= 2) btn.classList.add('speed-2x');
    if (P5._speedLevel >= 3) btn.classList.add('speed-3x');
  }
  P5.post({action:'SET_SPEED', speed: P5._speedLevel});
};

// ---- Autoplay toggle ----
P5._autoplay = false;
P5.toggleAutoplay = function() {
  P5._autoplay = !P5._autoplay;
  var btn = document.getElementById('btn-autoplay');
  if (btn) {
    if (P5._autoplay) btn.classList.add('active');
    else btn.classList.remove('active');
  }
  P5.post({action:'SET_AUTOPLAY'});
};

// ---- Bidding actions ----
P5.bidCall = function() { P5.post({action:'CALL'}); };
P5.bidPass = function() {
  P5.post({action:'PASS'});
};

// ---- Error toast ----
P5.showError = function(msg) {
  var el = document.getElementById('error-toast');
  if (!el) {
    el = document.createElement('div');
    el.id = 'error-toast';
    el.className = 'error-toast p5-font';
    document.body.appendChild(el);
  }
  el.textContent = msg;
  el.style.opacity = '1';
  clearTimeout(el._timer);
  el._timer = setTimeout(function() { el.style.opacity = '0'; }, 2000);
};

// ---- Mock state for testing ----
P5.mockState = function() {
  return {
    state:'PLAYING', my_cards:[0,1,4,5,8,9,12,13,18,22,26,30],
    player_card_counts:[8,14,5,10,12],
    my_seat:4, current_turn:4, is_landlord:true, landlords:[2,4],
    last_played:[], last_player:-1,
    bottom_cards:[48,49,51,53], multiplier:512,
    player_last_played:{0:[0,1], 2:[26]}
  };
};

})();
