let isShuffleEnabled = false;
let isSeeking = false;
let isPlaying = false;
let currentTrack = null;
let songsData = null;
let allTracks = [];
let playbackTimer = null;
let currentTime = 0;
let songDuration = 180;

function sendCommand(command) {
    console.log('Sending command:', command);
    alert('Command sent: ' + command);
}

function toggleShuffle() {
    isShuffleEnabled = !isShuffleEnabled;
    const btn = document.getElementById('shuffleBtn');
    const miniBtn = document.getElementById('miniShuffleBtn');

    if(isShuffleEnabled)
    {
        btn.style.background = '#22c55e';
        miniBtn?.classList.add('active');
        if(currentTrack == null)
            playRandomTrack();
    }
    else
    {
        btn.style.background = '#7c3aed';
        miniBtn?.classList.remove('active');
    }
    
    syncShuffleUI();
    console.log('Shuffle:', isShuffleEnabled);
}

function syncShuffleUI() {
    const miniBtn = document.getElementById('miniShuffleBtn');
    const mainBtn = document.getElementById('shuffleBtn');

    if (isShuffleEnabled) {
        miniBtn?.classList.add('active');
        mainBtn.style.background = '#22c55e';
    } else {
        miniBtn?.classList.remove('active');
        mainBtn.style.background = '#7c3aed';
    }
}

function playRandomTrack() {
    const randomIndex = Math.floor(Math.random() * allTracks.length);
    const randomTrack = allTracks[randomIndex];
    playTrack(randomTrack.id);
}

function playNextTrack() {
    if(!currentTrack) 
        return;

    if(isShuffleEnabled){
        playRandomTrack();
        return;
    }

    const currentIndex =
        allTracks.findIndex(
            track => track.id === currentTrack.id
        );

    let nextIndex = currentIndex + 1;
    if(nextIndex >= allTracks.length)
        nextIndex = 0;
    playTrack(allTracks[nextIndex].id);
}

function playPreviousTrack() {
    if(!currentTrack) 
        return;

    const currentIndex =
        allTracks.findIndex(
            track => track.id === currentTrack.id
        );

    let previousIndex = currentIndex - 1;
    if(previousIndex < 0) 
        previousIndex = allTracks.length - 1;
    playTrack(allTracks[previousIndex].id);
}

function playTrack(trackId) {
    let foundTrack = null;
    let foundAlbum = null;

    songsData.albums.forEach(album => {
        album.tracks.forEach(track => {
            if(track.id === trackId) {
                foundTrack = track;
                foundAlbum = album;
            }
        });
    });

    if(!foundTrack) 
        return;

    currentTrack = foundTrack;
    openOnlyThisAlbum(foundAlbum.name);
    document.querySelectorAll('.track').forEach(track => {
        track.classList.remove('active');
    });

    document.getElementById(`track-${trackId}`).classList.add('active');
    document.getElementById('currentSong').innerText = foundTrack.title;
    document.getElementById('currentAlbum').innerText = foundAlbum.name;
    const cover = document.getElementById('albumCover');
    cover.src = "albumCover/" + foundAlbum.cover;
    showCoverAndProgressbar(cover);

    isPlaying = true;
    currentTime = 0;
    updateProgressBar();    
    startPlayback();
    updateMiniPlayer(foundTrack, foundAlbum);
    console.log('Now playing:', foundTrack.title);
}

function showCoverAndProgressbar(cover){
    cover.classList.remove('hidden');
    document.getElementById('progressContainer').classList.remove('hidden');
}

function startPlayback() {
    clearInterval(playbackTimer);
    playbackTimer = setInterval(() => {
        currentTime++;
        updateProgressBar();
        if(currentTime >= songDuration) {
            clearInterval(playbackTimer);
            playNextTrack();
        }

    }, 1000);
}

function togglePlayback() {
    const btn = document.getElementById('playPauseBtn');
    const miniBtn = document.getElementById('miniPlayBtn');

    if(!currentTrack) 
        return;

    if(isPlaying){
        clearInterval(playbackTimer);
        isPlaying = false;
        btn.classList.add('paused');
        document.getElementById(`track-${currentTrack.id}`)?.classList.add('paused');
        miniBtn.innerText = "⏸";
        console.log('Paused');
    } 
    else 
    {
        startPlayback();
        isPlaying = true;
        btn.classList.remove('paused');
        document.getElementById(`track-${currentTrack.id}`)?.classList.remove('paused');
        miniBtn.innerText = "▶";
        console.log('Resumed');
    }
}

function updateProgressBar() {
    const bar = document.querySelector('.progress-fill');
    const timeDisplay = document.querySelector('.time-display');
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
            allTracks.push({
                id: track.id,
                title: track.title,
                album: album.name,
                cover: album.cover
            });
            tracksHTML += createTrackHTML(track, index);
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

function createTrackHTML(track, index) {
    return `
        <div class="track" id="track-${track.id}">
            <div class="track-left">
                <div class="track-number">${index + 1}</div>
                <div class="track-name">${track.title}</div>
            </div>

            <button onclick="playTrack(${track.id})">
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

function seekFromEvent(e) {
    const progressBar = document.querySelector('.progress');
    if(!progressBar || !currentTrack)
        return;

    const rect = progressBar.getBoundingClientRect();
    let x = e.clientX - rect.left;
    x = Math.max(0, Math.min(x, rect.width));
    const percent = x / rect.width;
    currentTime = percent * songDuration;
    updateProgressBar();
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

    document.getElementById('miniCover').src =
        "albumCover/" + album.cover;

    mini.classList.remove('hidden');
}

function seekMiniFromEvent(e) {
    const miniBar = document.querySelector('.mini-progress');
    if(!miniBar || !currentTrack) 
        return;

    const rect = miniBar.getBoundingClientRect();
    let x = e.clientX - rect.left;
    x = Math.max(0, Math.min(x, rect.width));
    const percent = x / rect.width;
    currentTime = percent * songDuration;
    updateProgressBar();
}

window.addEventListener('DOMContentLoaded', () => {
    loadSongs();
    initSeek('.progress');
    initSeek('.mini-progress');
});
