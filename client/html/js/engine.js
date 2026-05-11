// ===================================================================
// P5 Engine — Card encoding, hand rendering, player rendering
// ===================================================================

var P5 = P5 || {};

(function() {

const SUIT_MAP  = { D:'♦', C:'♣', H:'♥', S:'♠' };
const RANK_NAMES = ['3','4','5','6','7','8','9','10','J','Q','K','A','2','SJ','BJ'];
const PLAYER_NAMES = ['SKULL','PANTHER','FOX','QUEEN','JOKER'];
const PLAYER_POS = [
  {x:84,  y:239},   // pos 0 — left
  {x:434, y:87},    // pos 1 — top-left
  {x:1098,y:94},    // pos 2 — top-right
  {x:1409,y:303},   // pos 3 — right
  {x:43,  y:574}    // pos 4 — bottom-left (YOU)
];

let state = null;
let selectedSet = new Set();

// Track pass status & baseline for last_played cards
P5._playerPassed = {};
P5._lastPlayedBaseline = {};

P5.getState = function() { return state; };
P5._setState = function(s) { state = s; };

function cRank(c) { return Math.floor(c/4); }
function cSuit(c) { return c%4; }
function rankStr(c) {
  let r = cRank(c);
  if (c === 53) return 'SJ';
  if (c === 56) return 'BJ';
  return RANK_NAMES[r];
}
function suitStr(c) { return SUIT_MAP[['D','C','H','S'][cSuit(c)]]; }
function isRed(c) {
  if (c === 53) return false;
  if (c === 56) return true;
  let s = cSuit(c);
  return s === 0 || s === 2;
}
function isJoker(c) { return c === 53 || c === 56; }

function mySeat() {
  return (state && typeof state.my_seat === 'number') ? state.my_seat : 4;
}

function post(msg) {
  try {
    if (window.chrome && window.chrome.webview) {
      window.chrome.webview.postMessage(JSON.stringify(msg));
    } else {
      if (typeof P5 !== 'undefined' && P5.showError) P5.showError('NO_WEBVIEW');
    }
  } catch(e) {
    if (typeof P5 !== 'undefined' && P5.showError) P5.showError('SEND_ERR:' + e.message);
  }
}

// Expose helpers
P5.rankStr = rankStr;
P5.suitStr = suitStr;
P5.isRed = isRed;
P5.isJoker = isJoker;
P5.mySeat = mySeat;
P5.post = post;
P5.PLAYER_NAMES = PLAYER_NAMES;
P5.PLAYER_POS = PLAYER_POS;
P5.selectedSet = selectedSet;

// ===================================================================
// renderHand — Fan-shaped hand cards
// ===================================================================
P5.renderHand = function() {
  let ctr = document.getElementById('hand-container');
  if (!ctr) return;
  ctr.querySelectorAll('.card-wrapper').forEach(el => el.remove());
  if (!state || !state.my_cards) return;

  let cards = state.my_cards;
  let n = cards.length;
  let angleStep = 3.8;
  let startAngle = -((n-1) * angleStep) / 2;

  for (let i=0; i<n; i++) {
    let angle = startAngle + i * angleStep;
    let angleRad = angle * Math.PI / 180;
    let x = Math.sin(angleRad) * 1600;
    let bottomOffset = -30 - Math.abs(x) * 0.06;
    let isSel = selectedSet.has(i);

    let wrap = document.createElement('div');
    wrap.className = 'card-wrapper';
    if (isSel) {
      wrap.classList.add('selected');
      wrap.style.zIndex = 50;
      wrap.style.transform = 'translateY(-120px) rotate(0deg) scale(1.1)';
    } else {
      wrap.style.zIndex = 10;
      wrap.style.transform = 'rotate(' + angle + 'deg)';
    }
    wrap.style.left = 'calc(50% + ' + x + 'px - 60px)';
    wrap.style.bottom = bottomOffset + 'px';

    wrap.addEventListener('mouseenter', function() {
      if (!selectedSet.has(i)) wrap.style.transform = 'translateY(-40px) rotate(' + angle + 'deg)';
    });
    wrap.addEventListener('mouseleave', function() {
      if (!selectedSet.has(i)) wrap.style.transform = 'rotate(' + angle + 'deg)';
    });

    let cardVal = cards[i];
    let isCardRed = isRed(cardVal);
    let joker = isJoker(cardVal);

    let glow = document.createElement('div');
    glow.className = 'card-glow';

    let body = document.createElement('div');
    body.className = 'card-body ' + (isSel ? 'selected' : 'unselected');
    if (joker) body.classList.add('joker');
    if (joker && isCardRed) body.classList.add('big-joker');
    if (!isSel && isCardRed) body.classList.add('red');

    let tex = document.createElement('div');
    tex.className = 'card-texture';
    body.appendChild(tex);

    let tl = document.createElement('div');
    tl.className = 'card-topleft';
    if (joker) {
      tl.innerHTML = '<span class="tl-rank">' + rankStr(cardVal) + '</span><span class="tl-suit joker-star">★</span>';
    } else {
      tl.innerHTML = '<span class="tl-rank">' + rankStr(cardVal) + '</span><span class="tl-suit">' + suitStr(cardVal) + '</span>';
    }
    body.appendChild(tl);

    let center = document.createElement('div');
    center.className = 'card-center';
    if (joker) {
      center.textContent = '★';
      center.classList.add('joker-star-center');
    } else {
      center.textContent = suitStr(cardVal);
    }
    body.appendChild(center);

    wrap.appendChild(glow);
    wrap.appendChild(body);

    // Click: toggle selection
    wrap.addEventListener('click', function() {
      P5.playSFX('sfx_click.wav');
      if (selectedSet.has(i)) {
        selectedSet.delete(i);
        wrap.classList.remove('selected');
        body.className = 'card-body unselected' + (isCardRed ? ' red' : '') + (joker ? ' joker' : '') + (joker && isCardRed ? ' big-joker' : '');
        wrap.style.zIndex = 10;
        wrap.style.transform = 'rotate(' + angle + 'deg)';
        glow.style.display = 'none';
      } else {
        selectedSet.add(i);
        wrap.classList.add('selected');
        body.className = 'card-body selected' + (joker ? ' joker' : '') + (joker && isCardRed ? ' big-joker' : '');
        wrap.style.zIndex = 50;
        wrap.style.transform = 'translateY(-120px) rotate(0deg) scale(1.1)';
        glow.style.display = 'block';
      }
    });

    ctr.appendChild(wrap);
  }
};

// ===================================================================
// renderPlayers — 5 avatars with card counts, last played, scores
// ===================================================================
P5.renderPlayers = function() {
  let ctr = document.getElementById('player-nodes');
  if (!ctr) return;
  ctr.innerHTML = '';
  if (!state) return;

  let cnt = state.player_card_counts || [0,0,0,0,0];
  let ll = state.landlords || [];
  let turn = state.current_turn;
  let lp = state.player_last_played || {};
  let names = state.player_names || [];
  let avatars = state.player_avatars || [];
  let cumScores = state.cumulative_scores || [0,0,0,0,0];
  let me = mySeat();

  for (let pos=0; pos<5; pos++) {
    let i = (me + pos + 1) % 5;
    let p = PLAYER_POS[pos];
    let node = document.createElement('div');
    node.className = 'player-node';
    if (cnt[i] === 0) node.classList.add('spectral');
    if (i === turn && state.state === 'PLAYING') node.classList.add('current-turn');
    node.style.left = p.x + 'px';
    node.style.top  = p.y + 'px';

    let isLL = ll.includes(i);
    let displayName = names[i] || ((i === me) ? 'YOU' : PLAYER_NAMES[i]);
    let iconSymbol = isLL ? '♛' : '♟';
    let avatarColor = ['#dc2626','#2563eb','#16a34a','#f59e0b','#8b5cf6'][avatars[i] || 0];

    node.innerHTML =
      '<div class="player-box-wrap">' +
        '<div class="current-turn-ping"></div>' +
        '<div class="player-box" style="background:' + avatarColor + ';border-color:var(--white);">' +
          '<span class="box-icon p5-font" style="color:var(--white);">' + iconSymbol + '</span>' +
        '</div>' +
        '<div class="player-badge"><span class="badge-inner p5-font">' + cnt[i] + '</span></div>' +
      '</div>' +
      '<div class="player-name"><span class="p5-font">' + displayName + '</span>' +
        '<span class="player-role-tag p5-font" style="color:' + (isLL ? 'var(--blood)' : 'rgba(255,255,255,0.5)') + ';">' + (isLL ? '[地主]' : '[农民]') + '</span>' +
      '</div>' +
      '<div class="player-score p5-font">' + (cumScores[i] >= 0 ? '+' : '') + cumScores[i] + '</div>';

    // Last played / PASS indicator
    let isFresh = !P5._lastPlayedBaseline[i] || String(lp[i]) !== String(P5._lastPlayedBaseline[i]);
    if (state.last_player === -1) {
      // new round — nothing
    } else if (P5._playerPassed[i]) {
      let lpd = document.createElement('div');
      lpd.className = 'player-last-played';
      lpd.style.cssText = 'background:var(--black);color:var(--white);padding:4px 12px;border:2px solid var(--white);font-family:Impact,sans-serif;font-size:14px;font-style:italic;letter-spacing:2px;';
      lpd.textContent = 'PASS';
      node.appendChild(lpd);
    } else if (isFresh) {
      let lpc = lp[i];
      if (lpc && lpc.length > 0) {
        let lpd = document.createElement('div');
        lpd.className = 'player-last-played';
        for (let j = 0; j < lpc.length; j++) {
          let c = lpc[j];
          let tc = document.createElement('div');
          tc.className = 'tiny-card';
          if (isRed(c)) tc.classList.add('red');
          tc.innerHTML = '<span>' + rankStr(c) + '</span><span>' + suitStr(c) + '</span>';
          lpd.appendChild(tc);
        }
        node.appendChild(lpd);
      }
    }

    ctr.appendChild(node);
  }
};

// ===================================================================
// renderCenterPlayed
// ===================================================================
P5.renderCenterPlayed = function() {
  let ctr = document.getElementById('center-played');
  if (!ctr) return;
  ctr.innerHTML = '';
  if (!state) return;
  let cards = state.last_played || [];
  if (cards.length === 0) return;
  ctr.dataset.who = '';
  for (let i = 0; i < cards.length; i++) {
    let c = cards[i];
    let el = document.createElement('div');
    el.className = 'played-card';
    if (isRed(c)) el.classList.add('red');
    el.innerHTML = '<span class="p-rank">' + rankStr(c) + '</span><span class="p-suit">' + suitStr(c) + '</span>';
    ctr.appendChild(el);
  }
};

// Reset tracking state
P5.resetTracking = function() {
  P5._playerPassed = {};
  P5._lastPlayedBaseline = {};
};

})();
