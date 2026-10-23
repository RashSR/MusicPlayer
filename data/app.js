let isShuffleEnabled = false;
let isSeeking = false;
let isPlaying = false;
let currentTrack = null;
let songsData = null;
let allTracks = [];
let playbackTimer = null;
let currentTime = 0;
let songDuration = 180;
let currentSelectionId = 0;

const domElements = {
    currentSong: document.getElementById('currentSong'),
    currentAlbum: document.getElementById('currentAlbum'),
    playbackMessage: document.getElementById('playbackMessage'),
    albumCover: document.getElementById('albumCover'),
    progressFill: document.querySelector('.progress-fill'),
    timeDisplay: document.querySelector('.time-display'),
};

function sendCommand(command) {
    console.log('Sending command:', command);
    alert('Command sent: ' + command);
}

function toggleShuffle() {
    isShuffleEnabled = !isShuffleEnabled;
    const mainShuffleBtn = document.getElementById('shuffleBtn');
    const miniShuffleBtn = document.getElementById('miniShuffleBtn');

    if(isShuffleEnabled)
    {
        mainShuffleBtn.style.background = '#22c55e';
        miniShuffleBtn?.classList.add('active');
        if(currentTrack == null)
            playRandomTrack();
    }
    else
    {
        mainShuffleBtn.style.background = '#7c3aed';
        miniShuffleBtn?.classList.remove('active');
    }
    
    syncShuffleUI(mainShuffleBtn, miniShuffleBtn);
    console.log('Shuffle:', isShuffleEnabled);
}

function syncShuffleUI(mainShuffleBtn, miniShuffleBtn) {
    if(isShuffleEnabled)
    {
        miniShuffleBtn?.classList.add('active');
        mainShuffleBtn.style.background = '#22c55e';
    } 
    else 
    {
        miniShuffleBtn?.classList.remove('active');
        mainShuffleBtn.style.background = '#7c3aed';
    }
}

function playRandomTrack() {
    const randomIndex = Math.floor(Math.random() * allTracks.length);
    playTrackAtIndex(randomIndex);
}

function playNextTrack() {
    if(!currentTrack) 
        return;

    if(isShuffleEnabled){
        playRandomTrack();
        return;
    }

    const currentIndex = getCurrentTrackIndex();
    let nextIndex = currentIndex + 1;
    if(nextIndex >= allTracks.length)
        nextIndex = 0;
    playTrackAtIndex(nextIndex);
}

function playPreviousTrack() {
    if(!currentTrack) 
        return;

    const currentIndex = getCurrentTrackIndex();
    let previousIndex = currentIndex - 1;
    if(previousIndex < 0) 
        previousIndex = allTracks.length - 1;
    playTrackAtIndex(previousIndex);
}

function getCurrentTrackIndex(){
    return allTracks.indexOf(currentTrack);
}

function playTrackAtIndex(trackIndex) {
    const foundTrack = allTracks[trackIndex];
    if(!foundTrack)
        return;

    const foundAlbum = songsData.albums.find(
        album => album.name === foundTrack.album
    );
    if(!foundAlbum)
        return;

    const selectionId = ++currentSelectionId;
    currentTrack = foundTrack;
    openOnlyThisAlbum(foundAlbum.name);
    document.querySelectorAll('.track').forEach(track => {
        track.classList.remove('active');
    });

    document.getElementById(`track-${trackIndex}`).classList.add('active');
    domElements.currentSong.innerText = foundTrack.title;
    domElements.currentAlbum.innerText = foundAlbum.name;
    const cover = domElements.albumCover;
    cover.src = "albumCover/" + foundAlbum.cover;
    showCoverAndProgressbar(cover);

    clearPlaybackMessage();
    currentTime = 0;
    updateProgressBar();
    updateMiniPlayer(foundTrack, foundAlbum);
    startPlayback();
    console.log('[MusicPlayer] Selected song:', foundTrack.title);
    console.log('[MusicPlayer] Catalog ID:', foundTrack.id);
    console.log('[MusicPlayer] DFPlayer track:', foundTrack.dfplayerTrack);

    const hasMappedTrack = Number.isInteger(foundTrack.dfplayerTrack) &&
        foundTrack.dfplayerTrack >= 1 && foundTrack.dfplayerTrack <= 9999;
    const metadata = `album=${encodeURIComponent(foundTrack.album)}` +
        `&title=${encodeURIComponent(foundTrack.title)}` +
        `&catalogId=${encodeURIComponent(foundTrack.id)}`;
    const url = hasMappedTrack
        ? `/play?id=${encodeURIComponent(foundTrack.dfplayerTrack)}` +
            `&${metadata}&dfplayerTrack=${encodeURIComponent(foundTrack.dfplayerTrack)}`
        : `/api/play-selection?${metadata}`;

    if(!hasMappedTrack) {
        console.log('[MusicPlayer] No DFPlayer track mapping for song:', foundTrack.id);
        console.log('[MusicPlayer] DFPlayer playback request: not sent.');
        setPlaybackMessage('This song is not available on the SD card yet.');
    } else {
        console.log('[MusicPlayer] DFPlayer playback request: sending.');
    }

    console.log('[MusicPlayer] Selection metadata request: sending.');
    sendSelectionRequest(selectionId, url, hasMappedTrack);
}

async function sendSelectionRequest(selectionId, url, hasMappedTrack) {
    console.log('[MusicPlayer] Requesting', url);

    try {
        const response = await fetch(url);
        console.log('[MusicPlayer] HTTP response status:', response.status);
        if(currentSelectionId !== selectionId) {
            return;
        }
        if(response.status === 400) {
            setPlaybackMessage('The song selection was rejected (HTTP 400).');
        } else if(hasMappedTrack && response.status === 503) {
            setPlaybackMessage('DFPlayer could not send the command (HTTP 503). Try again shortly.');
        } else if(!response.ok) {
            setPlaybackMessage(`Song request was rejected (HTTP ${response.status}).`);
        } else if(!hasMappedTrack) {
            setPlaybackMessage('This song is not available on the SD card yet.');
        } else {
            setPlaybackMessage('Command sent to DFPlayer; audio playback is not confirmed.');
        }
    } catch(error) {
        if(currentSelectionId === selectionId) {
            setPlaybackMessage(hasMappedTrack
                ? 'Could not contact the ESP32; playback command was not confirmed.'
                : 'Could not contact the ESP32; song selection was not logged.');
        }
        console.error('[MusicPlayer] Selection request failed:', error);
    }
}

function setPlaybackMessage(message) {
    if(domElements.playbackMessage) {
        domElements.playbackMessage.textContent = message;
    }
}

function clearPlaybackMessage() {
    setPlaybackMessage('');
}

function showCoverAndProgressbar(cover){
    cover.classList.remove('hidden');
    document.getElementById('progressContainer').classList.remove('hidden');
}

function togglePlayback() {
    if(!currentTrack) 
        return;

    const trackIndex = getCurrentTrackIndex();
    const selectedRow = document.getElementById(`track-${trackIndex}`);
    const button = document.getElementById('playPauseBtn');
    const miniButton = document.getElementById('miniPlayBtn');

    if(isPlaying) {
        clearInterval(playbackTimer);
        playbackTimer = null;
        isPlaying = false;
        button.classList.add('paused');
        selectedRow?.classList.add('paused');
        miniButton.innerText = '▶';
        console.log('[MusicPlayer] Simulated timeline paused.');
    } else {
        startPlayback();
        console.log('[MusicPlayer] Simulated timeline resumed.');
    }
}

function startPlayback() {
    clearInterval(playbackTimer);
    isPlaying = true;
    document.getElementById('playPauseBtn').classList.remove('paused');
    document.getElementById(`track-${getCurrentTrackIndex()}`)?.classList.remove('paused');
    document.getElementById('miniPlayBtn').innerText = '⏸';

    playbackTimer = setInterval(() => {
        currentTime++;
        updateProgressBar();
        if(currentTime >= songDuration) {
            clearInterval(playbackTimer);
            playbackTimer = null;
            playNextTrack();
        }
    }, 1000);
}

function updateProgressBar() {
    const bar = domElements.progressFill;
    const timeDisplay = domElements.timeDisplay;
    const miniBar = document.querySelector('.mini-progress-fill');
    const miniTime = document.querySelector('.mini-time'); 
    const progress = (currentTime / songDuration) * 100;

    if(bar) 
        bar.style.width = `${progress}%`;

    if(miniBar)
        miniBar.style.width = `${progress}%`;

    const timeText =
        `${formatTime(currentTime)} / ${formatTime(songDuration)}`;

    if(timeDisplay)
        timeDisplay.textContent = timeText;

    if(miniTime)
        miniTime.textContent = timeText;
}

function toggleAlbum(header) {
    const trackList = header.nextElementSibling;
    const arrow = header.querySelector('.arrow');

    trackList.classList.toggle('collapsed');
    if(trackList.classList.contains('collapsed')) 
        arrow.innerHTML = '▶';
    else
        arrow.innerHTML = '▼';
}

async function loadSongs() {
    const response = await fetch('songs.json');
    songsData = await response.json();
    allTracks = [];
    const container = document.getElementById('albumsContainer');

    songsData.albums.forEach(album => {
        const albumDiv = document.createElement('div');
        albumDiv.className = 'album';

        let tracksHTML = '';

        album.tracks.forEach((track, index) => {
            const catalogIndex = allTracks.length;
            allTracks.push({
                id: track.id,
                title: track.title,
                album: album.name,
                cover: album.cover,
                dfplayerTrack: track.dfplayerTrack
            });
            tracksHTML += createTrackHTML(track, index, catalogIndex);
        });
        albumDiv.innerHTML = `
            <div class="album-header" onclick="toggleAlbum(this)">
                <h3>${album.name}</h3>

                <div>
                    <span class="arrow">▶</span>
                </div>
            </div>

            <div class="track-list collapsed">
                ${tracksHTML}
            </div>
        `;
        container.appendChild(albumDiv);
    });
}

function createTrackHTML(track, index, catalogIndex) {
    return `
        <div class="track" id="track-${catalogIndex}" onclick="playTrackAtIndex(${catalogIndex})">
            <div class="track-left">
                <div class="track-number">${index + 1}</div>
                <div class="track-name">${track.title}</div>
            </div>

            <button onclick="event.stopPropagation(); playTrackAtIndex(${catalogIndex})">
                Play
            </button>
        </div>
    `;
}

function formatTime(seconds) {
    const min = Math.floor(seconds / 60);
    const sec = Math.floor(seconds % 60);
    return `${String(min).padStart(2, '0')}:${String(sec).padStart(2, '0')}`;
}

function initSeek(barSelector) {
    const bar = document.querySelector(barSelector);
    if(!bar)
        return;

    function handleSeek(e){
        const rect = bar.getBoundingClientRect();
        let clientX = e.clientX;
        if (e.touches)
            clientX = e.touches[0].clientX;

        let x = clientX - rect.left;
        x = Math.max(0, Math.min(x, rect.width));
        const percent = x / rect.width;
        currentTime = percent * songDuration;
        updateProgressBar();
    }

    bar.addEventListener('click', handleSeek);

    bar.addEventListener('mousedown', (e) => {
        isSeeking = true;
        handleSeek(e);
    });

    document.addEventListener('mousemove', (e) => {
        if (!isSeeking)
            return;

        handleSeek(e);
    });

    document.addEventListener('mouseup', () => {
        isSeeking = false;
    });

    bar.addEventListener('touchstart', (e) => {
        isSeeking = true;
        handleSeek(e);
    });

    document.addEventListener('touchmove', (e) => {
        if (!isSeeking)
            return;

        handleSeek(e);
    });

    document.addEventListener('touchend', () => {
        isSeeking = false;
    });
}

function openOnlyThisAlbum(albumName) {

    const albums = document.querySelectorAll('.album');

    albums.forEach(album => {

        const header = album.querySelector('.album-header');
        const trackList = album.querySelector('.track-list');
        const arrow = album.querySelector('.arrow');

        const name = header.querySelector('h3').innerText;

        if(name === albumName) {
            trackList.classList.remove('collapsed');
            arrow.innerHTML = '▼';
        } else {
            trackList.classList.add('collapsed');
            arrow.innerHTML = '▶';
        }
    });
}

function updateMiniPlayer(track, album) {
    const mini = document.getElementById('miniPlayer');
    document.getElementById('miniTitle').innerText = track.title;
    document.getElementById('miniAlbum').innerText = album.name;
    document.getElementById('miniCover').src = "albumCover/" + album.cover;
    mini.classList.remove('hidden');
}

window.addEventListener('DOMContentLoaded', () => {
    loadSongs();
    initSeek('.progress');
    initSeek('.mini-progress');
});

window.addEventListener('error', event => {
    console.error('[MusicPlayer] JavaScript exception:', event.error || event.message);
});

window.addEventListener('unhandledrejection', event => {
    console.error('[MusicPlayer] Unhandled promise rejection:', event.reason);
});
