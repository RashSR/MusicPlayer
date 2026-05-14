let isShuffleEnabled = false;
let isPlaying = false;
let currentTrack = null;

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

function playTrack(trackNumber) {
    console.log('Playing track:', trackNumber);

    // Later:
    // fetch('/play?num=' + trackNumber)

    alert('Playing track: ' + trackNumber);
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