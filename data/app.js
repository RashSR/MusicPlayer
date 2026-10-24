let isShuffleEnabled = false;
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

    if(!Number.isInteger(foundTrack.id) ||
        foundTrack.id < 1 || foundTrack.id > 9999) {
        setPlaybackMessage('This song has an invalid catalog ID.');
        console.error('[MusicPlayer] Invalid catalog ID:', foundTrack.id);
        return;
    }

    const url = `/api/play-selection?id=${encodeURIComponent(foundTrack.id)}`;
    console.log('[MusicPlayer] Catalog selection request: sending.');
    sendSelectionRequest(selectionId, url);
}

async function sendSelectionRequest(selectionId, url) {
    console.log('[MusicPlayer] Requesting', url);

    try {
        const response = await fetch(url);
        console.log('[MusicPlayer] HTTP response status:', response.status);
        if(currentSelectionId !== selectionId) {
            return;
        }
        if(response.status === 400 || response.status === 404) {
            setPlaybackMessage('The catalog song ID was rejected or not found.');
        } else if(response.status === 409) {
            setPlaybackMessage('Duplicate playback request ignored.');
        } else if(response.status === 503) {
            setPlaybackMessage('DFPlayer could not send the command (HTTP 503). Try again shortly.');
        } else if(response.status === 500) {
            setPlaybackMessage('The ESP32 could not read the song catalog.');
        } else if(!response.ok) {
            setPlaybackMessage(`Playback request was rejected (HTTP ${response.status}).`);
        } else {
            setPlaybackMessage('Playback command sent; SD-card file availability is not confirmed.');
        }
    } catch(error) {
        if(currentSelectionId === selectionId) {
            setPlaybackMessage('Could not contact the ESP32; playback command was not confirmed.');
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
        requestPlaybackControl('/pause');
    } else {
        startPlayback();
        console.log('[MusicPlayer] Simulated timeline resumed.');
        requestPlaybackControl('/resume');
    }
}

async function requestPlaybackControl(path) {
    try {
        const response = await fetch(path);
        if(!response.ok) {
            console.error('[MusicPlayer] Playback control failed:',
                path, response.status);
        }
    } catch(error) {
        console.error('[MusicPlayer] Playback control request failed:',
            path, error);
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
                ledColors: album.ledColors
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
});

window.addEventListener('error', event => {
    console.error('[MusicPlayer] JavaScript exception:', event.error || event.message);
});

window.addEventListener('unhandledrejection', event => {
    console.error('[MusicPlayer] Unhandled promise rejection:', event.reason);
});
