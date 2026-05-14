let isShuffleEnabled = false;
let isPlaying = false;
let currentTrack = null;
let songsData = null;

function sendCommand(command) {
    console.log('Sending command:', command);
    alert('Command sent: ' + command);
}

function toggleShuffle() {
    isShuffleEnabled = !isShuffleEnabled;
    const btn = document.getElementById('shuffleBtn');

    if(isShuffleEnabled) {
        btn.style.background = '#22c55e';
    } else {
        btn.style.background = '#7c3aed';
    }

    console.log('Shuffle:', isShuffleEnabled);
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

    if(!foundTrack) return;

    currentTrack = foundTrack;

    document.getElementById('currentSong').innerText =
        foundTrack.title;

    document.getElementById('currentAlbum').innerText =
        foundAlbum.name;

    document.getElementById('albumCover').src =
        foundAlbum.cover;

    console.log('Now playing:', foundTrack.title);

    fetch('/play?track=' + trackId);
}

function toggleAlbum(header) {
    const trackList = header.nextElementSibling;
    const arrow = header.querySelector('.arrow');

    trackList.classList.toggle('collapsed');

    if(trackList.classList.contains('collapsed')) {
        arrow.innerHTML = '▶';
    } else {
        arrow.innerHTML = '▼';
    }
}

async function loadSongs() {

    const response = await fetch('songs.json');
    songsData = await response.json();

    const container = document.getElementById('albumsContainer');

    songsData.albums.forEach(album => {

        const albumDiv = document.createElement('div');
        albumDiv.className = 'album';

        let tracksHTML = '';

        album.tracks.forEach((track, index) => {

            tracksHTML += `
                <div class="track">
                    <div class="track-left">
                        <div class="track-number">${index + 1}</div>
                        <div class="track-name">${track.title}</div>
                    </div>

                    <button onclick="playTrack(${track.id})">
                        Play
                    </button>
                </div>
            `;
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

loadSongs();