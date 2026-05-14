    function sendCommand(command) {
        console.log('Sending command:', command);

        // Later:
        // fetch('/' + command)

        alert('Command sent: ' + command);
    }

    let shuffleEnabled = false;
    function toggleShuffle() {
        shuffleEnabled = !shuffleEnabled;
        const btn = document.getElementById('shuffleBtn');

        if(shuffleEnabled) {
            btn.style.background = '#22c55e';
        } else {
            btn.style.background = '#7c3aed';
        }

        console.log('Shuffle:', shuffleEnabled);
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