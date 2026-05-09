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
var bottomPickFlipping = false;

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
  if (st.state === 'BOTTOM_PICK') {
    if (bpArea) bpArea.style.display = 'flex';
    P5.renderBottomPickCards(st);
    var confirmBtn = document.getElementById('btn-confirm-pick');
    if (confirmBtn) confirmBtn.style.display = st.is_picking ? 'block' : 'none';
  } else if (!bottomPickFlipping) {
    if (bpArea) bpArea.style.display = 'none';
    bottomPickSelected = [];
  }

  // ================================================================
  // Top mini bottom cards bar (during PLAYING/END)
  // ================================================================
  var miniBar = document.getElementById('bottom-mini-bar');
  if (st.state === 'PLAYING' || st.state === 'END') {
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
  if (bottomPickSelected.length !== 2) return;
  // Animate flip on selected cards
  var cards = document.querySelectorAll('.pick-card-back.selected');
  for (var k = 0; k < cards.length; k++) {
    cards[k].classList.add('flip-card');
  }
  // Freeze area for 600ms so flip animation can play before server hides it
  bottomPickFlipping = true;
  setTimeout(function() { bottomPickFlipping = false; }, 600);
  P5.post({action:'PICK_BOTTOM', indices: bottomPickSelected.slice()});
  bottomPickSelected = [];
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
    var llA = landlords[0];
    var llB = landlords[1];
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
