let isShuffleEnabled = false;
let isPlaying = false;
let currentTrack = null;
let songsData = null;
let allTracks = [];
let playbackTimer = null;
let currentTime = 0;
const songDuration = 127;

function sendCommand(command) {
    console.log('Sending command:', command);
    alert('Command sent: ' + command);
}

function toggleShuffle() {
    isShuffleEnabled = !isShuffleEnabled;
    const btn = document.getElementById('shuffleBtn');

    if(isShuffleEnabled)
    {
        btn.style.background = '#22c55e';
        playRandomTrack();
    }
    else 
        btn.style.background = '#7c3aed';

    console.log('Shuffle:', isShuffleEnabled);
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
    if(!currentTrack) 
        return;

    if(isPlaying){
        clearInterval(playbackTimer);
        isPlaying = false;
        console.log('Paused');
    } 
    else 
    {
        startPlayback();
        isPlaying = true;
        console.log('Resumed');
    }
}

function updateProgressBar() {
    const bar = document.querySelector('.progress-fill');
    const timeDisplay = document.querySelector('.time-display');
    if(!bar || !timeDisplay) 
        return;

    const progress = (currentTime / songDuration) * 100;
    bar.style.width = `${progress}%`;
    timeDisplay.textContent = `${formatTime(currentTime)} / ${formatTime(songDuration)}`;
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

loadSongs();