let isShuffleEnabled = false;
let isPlaying = false;
let currentTrack = null;
let songsData = null;
let allTracks = [];
let playbackTimer = null;
let currentTime = 0;
let songDuration = null;
let currentSelectionId = 0;
let pendingPlaybackSelectionId = 0;
let lastPlaybackSyncAt = 0;
let playbackControlInProgress = false;
let songsCatalogLoaded = false;
let volumeUpdateTimer = null;
let volumeRequestSequence = 0;
let ledsEnabled = null;
let ledRequestInProgress = false;
let ledRequestSequence = 0;
let statusSyncInProgress = false;
let statusSyncPending = false;
let ledBrightnessUpdateTimer = null;
let ledBrightnessRequestSequence = 0;
let ledBrightnessRequestInProgress = false;
let pendingLedBrightness = null;
let ledSaturationUpdateTimer = null;
let ledSaturationRequestSequence = 0;
let ledSaturationRequestInProgress = false;
let pendingLedSaturation = null;

const domElements = {
    currentSong: document.getElementById('currentSong'),
    currentAlbum: document.getElementById('currentAlbum'),
    playbackMessage: document.getElementById('playbackMessage'),
    albumCover: document.getElementById('albumCover'),
    progressFill: document.querySelector('.progress-fill'),
    timeDisplay: document.querySelector('.time-display'),
    volumeSlider: document.getElementById('volumeSlider'),
    volumeValue: document.getElementById('volumeValue'),
    ledToggle: document.getElementById('ledToggle'),
    ledStatusMessage: document.getElementById('ledStatusMessage'),
    ledBrightnessSlider: document.getElementById('ledBrightnessSlider'),
    ledBrightnessValue: document.getElementById('ledBrightnessValue'),
    ledBrightnessMessage: document.getElementById('ledBrightnessMessage'),
    ledSaturationSlider: document.getElementById('ledSaturationSlider'),
    ledSaturationValue: document.getElementById('ledSaturationValue'),
    ledSaturationMessage: document.getElementById('ledSaturationMessage'),
};

function updateVolumeDisplay(volume) {
    domElements.volumeSlider.value = String(volume);
    domElements.volumeValue.textContent = String(volume);
}

async function sendVolumeUpdate(volume, sequence) {
    try {
        const response = await fetch(`/api/volume?volume=${encodeURIComponent(volume)}`);
        if(!response.ok) {
            console.error('[MusicPlayer] Volume update failed:', response.status);
            return;
        }
        const status = await response.json();
        if(sequence === volumeRequestSequence &&
            Number.isInteger(status.volume) &&
            status.volume >= 0 && status.volume <= 30) {
            updateVolumeDisplay(status.volume);
        }
    } catch(error) {
        console.error('[MusicPlayer] Volume update request failed:', error);
    }
}

function scheduleVolumeUpdate(immediate = false) {
    const volume = Number(domElements.volumeSlider.value);
    const sequence = ++volumeRequestSequence;
    domElements.volumeValue.textContent = String(volume);
    clearTimeout(volumeUpdateTimer);
    if(immediate) {
        volumeUpdateTimer = null;
        sendVolumeUpdate(volume, sequence);
    } else {
        volumeUpdateTimer = setTimeout(() => {
            volumeUpdateTimer = null;
            sendVolumeUpdate(volume, sequence);
        }, 150);
    }
}

function updateLedDisplay(enabled) {
    ledsEnabled = enabled;
    domElements.ledToggle.disabled = false;
    domElements.ledToggle.setAttribute('aria-pressed', String(enabled));
    domElements.ledToggle.classList.toggle('leds-off', !enabled);
    domElements.ledToggle.textContent = enabled ? '💡 LEDs On' : '💡 LEDs Off';
    domElements.ledStatusMessage.textContent = '';
}

function markLedStateUnavailable() {
    ledsEnabled = null;
    domElements.ledToggle.disabled = true;
    domElements.ledToggle.setAttribute('aria-pressed', 'mixed');
    domElements.ledToggle.classList.remove('leds-off');
    domElements.ledToggle.textContent = '💡 LEDs unavailable';
    domElements.ledBrightnessSlider.disabled = true;
    domElements.ledBrightnessValue.textContent = '--';
    domElements.ledBrightnessMessage.textContent = 'LED brightness unavailable';
    domElements.ledSaturationSlider.disabled = true;
    domElements.ledSaturationValue.textContent = '--';
    domElements.ledSaturationMessage.textContent = 'LED saturation unavailable';
}

function updateLedBrightnessDisplay(brightness) {
    domElements.ledBrightnessSlider.value = String(brightness);
    domElements.ledBrightnessSlider.disabled = false;
    domElements.ledBrightnessValue.textContent = String(brightness);
    domElements.ledBrightnessMessage.textContent = '';
}

function updateLedSaturationDisplay(saturation) {
    domElements.ledSaturationSlider.value = String(saturation);
    domElements.ledSaturationSlider.disabled = false;
    domElements.ledSaturationValue.textContent = String(saturation);
    domElements.ledSaturationMessage.textContent = '';
}

function scheduleLedSaturationUpdate(immediate = false) {
    const saturation = Number(domElements.ledSaturationSlider.value);
    const sequence = ++ledSaturationRequestSequence;
    domElements.ledSaturationValue.textContent = String(saturation);
    clearTimeout(ledSaturationUpdateTimer);
    if(immediate) {
        ledSaturationUpdateTimer = null;
        sendLedSaturationUpdate(saturation, sequence);
    } else {
        ledSaturationUpdateTimer = setTimeout(() => {
            ledSaturationUpdateTimer = null;
            sendLedSaturationUpdate(saturation, sequence);
        }, 150);
    }
}

async function sendLedSaturationUpdate(saturation, sequence) {
    if(ledSaturationRequestInProgress) {
        pendingLedSaturation = {saturation, sequence};
        return;
    }

    ledSaturationRequestInProgress = true;
    try {
        const response = await fetch('/api/leds/saturation', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded',
            },
            body: new URLSearchParams({
                saturation: String(saturation),
            }),
        });
        if(!response.ok) {
            console.error('[MusicPlayer] LED saturation update failed:',
                response.status);
            throw new Error(`LED saturation update failed (HTTP ${response.status})`);
        }

        const status = await response.json();
        if(!Number.isInteger(status.led_saturation) ||
            status.led_saturation < 0 || status.led_saturation > 150) {
            throw new Error('LED saturation response has no valid value');
        }
        if(sequence === ledSaturationRequestSequence) {
            updateLedSaturationDisplay(status.led_saturation);
        }
    } catch(error) {
        console.error('[MusicPlayer] LED saturation request failed:', error);
        domElements.ledSaturationMessage.textContent =
            'Could not confirm saturation; checking device...';
    } finally {
        ledSaturationRequestInProgress = false;
        if(pendingLedSaturation !== null) {
            const pending = pendingLedSaturation;
            pendingLedSaturation = null;
            sendLedSaturationUpdate(pending.saturation, pending.sequence);
        } else if(ledSaturationUpdateTimer === null) {
            await syncDeviceStatus();
        }
    }
}

function scheduleLedBrightnessUpdate(immediate = false) {
    const brightness = Number(domElements.ledBrightnessSlider.value);
    const sequence = ++ledBrightnessRequestSequence;
    domElements.ledBrightnessValue.textContent = String(brightness);
    clearTimeout(ledBrightnessUpdateTimer);
    if(immediate) {
        ledBrightnessUpdateTimer = null;
        sendLedBrightnessUpdate(brightness, sequence);
    } else {
        ledBrightnessUpdateTimer = setTimeout(() => {
            ledBrightnessUpdateTimer = null;
            sendLedBrightnessUpdate(brightness, sequence);
        }, 150);
    }
}

async function sendLedBrightnessUpdate(brightness, sequence) {
    if(ledBrightnessRequestInProgress) {
        pendingLedBrightness = {brightness, sequence};
        return;
    }

    ledBrightnessRequestInProgress = true;
    try {
        const response = await fetch('/api/leds/brightness', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded',
            },
            body: new URLSearchParams({
                brightness: String(brightness),
            }),
        });
        if(!response.ok) {
            console.error('[MusicPlayer] LED brightness update failed:',
                response.status);
            throw new Error(`LED brightness update failed (HTTP ${response.status})`);
        }

        const status = await response.json();
        if(!Number.isInteger(status.led_brightness) ||
            status.led_brightness < 0 || status.led_brightness > 100) {
            throw new Error('LED brightness response has no valid value');
        }
        if(sequence === ledBrightnessRequestSequence) {
            updateLedBrightnessDisplay(status.led_brightness);
        }
    } catch(error) {
        console.error('[MusicPlayer] LED brightness request failed:', error);
        domElements.ledBrightnessMessage.textContent =
            'Could not confirm brightness; checking device...';
    } finally {
        ledBrightnessRequestInProgress = false;
        if(pendingLedBrightness !== null) {
            const pending = pendingLedBrightness;
            pendingLedBrightness = null;
            sendLedBrightnessUpdate(pending.brightness, pending.sequence);
        } else if(ledBrightnessUpdateTimer === null) {
            await syncDeviceStatus();
        }
    }
}

async function syncDeviceStatus() {
    if(statusSyncInProgress) {
        statusSyncPending = true;
        return;
    }
    statusSyncInProgress = true;
    const sequence = volumeRequestSequence;
    const ledSequence = ledRequestSequence;
    const brightnessSequence = ledBrightnessRequestSequence;
    const saturationSequence = ledSaturationRequestSequence;
    const playbackSequence = currentSelectionId;
    try {
        const response = await fetch('/api/status');
        if(!response.ok) {
            console.error('[MusicPlayer] Status request failed:', response.status);
            if(!ledRequestInProgress && ledSequence === ledRequestSequence) {
                markLedStateUnavailable();
            }
            return;
        }
        const status = await response.json();
        if(songsCatalogLoaded &&
            playbackSequence === currentSelectionId &&
            (pendingPlaybackSelectionId === 0 ||
                pendingPlaybackSelectionId !== playbackSequence)) {
            applyPlaybackStatus(status);
        }
        if(sequence === volumeRequestSequence &&
            volumeUpdateTimer === null &&
            Number.isInteger(status.volume) &&
            status.volume >= 0 && status.volume <= 30) {
            updateVolumeDisplay(status.volume);
        }
        if(!ledRequestInProgress && ledSequence === ledRequestSequence) {
            if(typeof status.leds_enabled === 'boolean') {
                updateLedDisplay(status.leds_enabled);
            } else {
                console.error('[MusicPlayer] Status response has no valid LED state.');
                markLedStateUnavailable();
            }
        }
        if(!ledBrightnessRequestInProgress &&
            ledBrightnessUpdateTimer === null &&
            brightnessSequence === ledBrightnessRequestSequence) {
            if(Number.isInteger(status.led_brightness) &&
                status.led_brightness >= 0 && status.led_brightness <= 100) {
                updateLedBrightnessDisplay(status.led_brightness);
            } else {
                console.error('[MusicPlayer] Status response has no valid LED brightness.');
                markLedStateUnavailable();
            }
        }
        if(!ledSaturationRequestInProgress &&
            ledSaturationUpdateTimer === null &&
            saturationSequence === ledSaturationRequestSequence) {
            if(Number.isInteger(status.led_saturation) &&
                status.led_saturation >= 0 && status.led_saturation <= 150) {
                updateLedSaturationDisplay(status.led_saturation);
            } else {
                console.error('[MusicPlayer] Status response has no valid LED saturation.');
                markLedStateUnavailable();
            }
        }
    } catch(error) {
        console.error('[MusicPlayer] Status request failed:', error);
        if(!ledRequestInProgress && ledSequence === ledRequestSequence) {
            markLedStateUnavailable();
        }
    } finally {
        statusSyncInProgress = false;
        if(statusSyncPending) {
            statusSyncPending = false;
            syncDeviceStatus();
        }
    }
}

async function toggleLeds() {
    if(ledsEnabled === null || ledRequestInProgress) {
        return;
    }

    const requestedState = !ledsEnabled;
    ++ledRequestSequence;
    ledRequestInProgress = true;
    domElements.ledToggle.disabled = true;
    domElements.ledStatusMessage.textContent = 'Updating...';
    try {
        const response = await fetch('/api/leds', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded',
            },
            body: new URLSearchParams({
                enabled: String(requestedState),
            }),
        });
        if(!response.ok) {
            console.error('[MusicPlayer] LED update failed:', response.status);
            throw new Error(`LED update failed (HTTP ${response.status})`);
        }

        const status = await response.json();
        if(typeof status.leds_enabled !== 'boolean') {
            throw new Error('LED update response has no valid state');
        }
        updateLedDisplay(status.leds_enabled);
    } catch(error) {
        console.error('[MusicPlayer] LED update request failed:', error);
        markLedStateUnavailable();
        domElements.ledStatusMessage.textContent =
            'Could not confirm LED state; checking device...';
    } finally {
        ledRequestInProgress = false;
        if(ledsEnabled === null) {
            await syncDeviceStatus();
        } else {
            domElements.ledToggle.disabled = false;
        }
    }
}

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
    setShuffleEnabled(isShuffleEnabled);
    console.log('Shuffle:', isShuffleEnabled);
}

async function setShuffleEnabled(enabled) {
    try {
        const response = await fetch('/api/shuffle', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/x-www-form-urlencoded',
            },
            body: new URLSearchParams({enabled: String(enabled)}),
        });
        if(!response.ok) {
            throw new Error(`Shuffle update failed (HTTP ${response.status})`);
        }
        const status = await response.json();
        isShuffleEnabled = status.shuffle_enabled === true;
        syncShuffleUI(
            document.getElementById('shuffleBtn'),
            document.getElementById('miniShuffleBtn')
        );
    } catch(error) {
        console.error('[MusicPlayer] Shuffle update failed:', error);
        await syncDeviceStatus();
    }
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
    if(currentTrack) {
        requestPlaybackControl(
            `/next?expected_id=${encodeURIComponent(currentTrack.id)}`
        );
    }
}

function playPreviousTrack() {
    if(currentTrack) {
        requestPlaybackControl(
            `/previous?expected_id=${encodeURIComponent(currentTrack.id)}`
        );
    }
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
    songDuration = foundTrack.duration;
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
    lastPlaybackSyncAt = performance.now();
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
    pendingPlaybackSelectionId = selectionId;
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
    } finally {
        if(currentSelectionId === selectionId) {
            pendingPlaybackSelectionId = 0;
            await syncDeviceStatus();
        }
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

    requestPlaybackControl(isPlaying ? '/pause' : '/resume');
}

async function requestPlaybackControl(path) {
    if(playbackControlInProgress) {
        return;
    }
    playbackControlInProgress = true;
    try {
        const response = await fetch(path);
        if(!response.ok) {
            console.error('[MusicPlayer] Playback control failed:',
                path, response.status);
            await syncDeviceStatus();
        } else {
            await syncDeviceStatus();
        }
    } catch(error) {
        console.error('[MusicPlayer] Playback control request failed:',
            path, error);
    } finally {
        playbackControlInProgress = false;
    }
}

function startPlayback() {
    clearInterval(playbackTimer);
    isPlaying = true;
    setPlaybackControls();
    playbackTimer = setInterval(() => {
        if(isPlaying && lastPlaybackSyncAt > 0) {
            currentTime += (performance.now() - lastPlaybackSyncAt) / 1000;
            lastPlaybackSyncAt = performance.now();
        }
        updateProgressBar();
    }, 500);
}

function setPlaybackControls() {
    const trackIndex = getCurrentTrackIndex();
    const button = document.getElementById('playPauseBtn');
    const miniButton = document.getElementById('miniPlayBtn');
    button.classList.toggle('paused', !isPlaying);
    document.getElementById(`track-${trackIndex}`)?.classList.toggle(
        'paused', !isPlaying
    );
    miniButton.innerText = isPlaying ? '⏸' : '▶';
}

function renderNowPlaying(track) {
    const album = songsData.albums.find(item => item.name === track.album);
    if(!album) {
        return;
    }

    currentTrack = track;
    openOnlyThisAlbum(album.name);
    document.querySelectorAll('.track').forEach(row => {
        row.classList.remove('active', 'paused');
    });
    const trackIndex = getCurrentTrackIndex();
    const selectedRow = document.getElementById(`track-${trackIndex}`);
    selectedRow?.classList.add('active');
    domElements.currentSong.innerText = track.title;
    domElements.currentAlbum.innerText = album.name;
    domElements.albumCover.src = `albumCover/${album.cover}`;
    showCoverAndProgressbar(domElements.albumCover);
    updateMiniPlayer(track, album);
}

function applyPlaybackStatus(status) {
    if(typeof status.shuffle_enabled === 'boolean' &&
        isShuffleEnabled !== status.shuffle_enabled) {
        isShuffleEnabled = status.shuffle_enabled;
        syncShuffleUI(
            document.getElementById('shuffleBtn'),
            document.getElementById('miniShuffleBtn')
        );
    }

    const reportedSongId = status.current_track_id ?? status.song_id;
    const songId = Number(reportedSongId);
    const track = Number.isInteger(songId)
        ? allTracks.find(item => item.id === songId)
        : null;
    if(!track) {
        if(reportedSongId === null || reportedSongId === undefined) {
            currentTrack = null;
            isPlaying = false;
            currentTime = 0;
            songDuration = null;
            clearInterval(playbackTimer);
            playbackTimer = null;
            document.querySelectorAll('.track').forEach(row => {
                row.classList.remove('active', 'paused');
            });
            domElements.currentSong.innerText = 'No song selected';
            domElements.currentAlbum.innerText = 'Choose a track';
            domElements.albumCover.classList.add('hidden');
            document.getElementById('progressContainer').classList.add('hidden');
            document.getElementById('miniPlayer').classList.add('hidden');
            setPlaybackControls();
        }
        return;
    }

    if(!currentTrack || currentTrack.id !== track.id) {
        renderNowPlaying(track);
    }
    const state = typeof status.playback_state === 'string'
        ? status.playback_state
        : (status.playing ? 'playing' : 'stopped');
    isPlaying = state === 'playing';
    const reportedElapsed = Number(status.elapsed_seconds);
    currentTime = Number.isFinite(reportedElapsed) && reportedElapsed >= 0
        ? reportedElapsed
        : 0;
    const reportedDuration = Number(status.duration_seconds);
    songDuration = Number.isFinite(reportedDuration) && reportedDuration > 0
        ? reportedDuration
        : track.duration;
    lastPlaybackSyncAt = performance.now();
    setPlaybackControls();
    if(isPlaying && playbackTimer === null) {
        startPlayback();
    }
    updateProgressBar();
}

function updateProgressBar() {
    const bar = domElements.progressFill;
    const timeDisplay = domElements.timeDisplay;
    const miniBar = document.querySelector('.mini-progress-fill');
    const miniTime = document.querySelector('.mini-time'); 
    const progress = songDuration === null
        ? 0
        : Math.min(currentTime / songDuration * 100, 100);
    const displayTime = songDuration === null
        ? currentTime
        : Math.min(currentTime, songDuration);

    if(bar) 
        bar.style.width = `${progress}%`;

    if(miniBar)
        miniBar.style.width = `${progress}%`;

    const durationText = songDuration === null
        ? '--:--'
        : formatTime(songDuration);
    const timeText = `${formatTime(displayTime)} / ${durationText}`;

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
    if(!response.ok) {
        throw new Error(`Song catalog request failed (HTTP ${response.status})`);
    }
    songsData = await response.json();
    if(!Array.isArray(songsData.albums)) {
        throw new Error('Song catalog response has no albums array');
    }
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
                duration: Number.isInteger(track.duration) && track.duration > 0
                    ? track.duration
                    : null,
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
    songsCatalogLoaded = true;
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

window.addEventListener('DOMContentLoaded', async () => {
    domElements.volumeSlider.addEventListener('input', () => scheduleVolumeUpdate());
    domElements.volumeSlider.addEventListener('change', () => scheduleVolumeUpdate(true));
    domElements.ledToggle.addEventListener('click', toggleLeds);
    domElements.ledBrightnessSlider.addEventListener(
        'input', () => scheduleLedBrightnessUpdate());
    domElements.ledBrightnessSlider.addEventListener(
        'change', () => scheduleLedBrightnessUpdate(true));
    domElements.ledSaturationSlider.addEventListener(
        'input', () => scheduleLedSaturationUpdate());
    domElements.ledSaturationSlider.addEventListener(
        'change', () => scheduleLedSaturationUpdate(true));
    setInterval(syncDeviceStatus, 5000);
    document.addEventListener('visibilitychange', () => {
        if(document.visibilityState === 'visible') {
            syncDeviceStatus();
        }
    });
    window.addEventListener('focus', syncDeviceStatus);
    try {
        await Promise.all([loadSongs(), syncDeviceStatus()]);
        await syncDeviceStatus();
    } catch(error) {
        console.error('[MusicPlayer] Initialization failed:', error);
        setPlaybackMessage('Could not load the song catalog or device status.');
    }
});

window.addEventListener('error', event => {
    console.error('[MusicPlayer] JavaScript exception:', event.error || event.message);
});

window.addEventListener('unhandledrejection', event => {
    console.error('[MusicPlayer] Unhandled promise rejection:', event.reason);
});
