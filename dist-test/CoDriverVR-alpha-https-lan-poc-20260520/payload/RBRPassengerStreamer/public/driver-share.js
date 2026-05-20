import { setStatus } from './common.js';

const inviteInput = document.querySelector('#invite');
const shareButton = document.querySelector('#share');
const copyButton = document.querySelector('#copy');
const messengerButton = document.querySelector('#messenger');
const facebookButton = document.querySelector('#facebook');
const openInviteLink = document.querySelector('#open-invite');
const closePageButton = document.querySelector('#close-page');
const status = document.querySelector('#status');

function params() {
  return new URLSearchParams(location.search);
}

function inviteUrl() {
  return params().get('invite') ?? '';
}

function controlToken() {
  return params().get('control') ?? '';
}

function selectInvite() {
  inviteInput.focus();
  inviteInput.select();
  inviteInput.setSelectionRange(0, inviteInput.value.length);
}

function setReadyStatus(message = 'Invite ready.') {
  const secureHint = window.isSecureContext ? '' : ' Browser sharing/copy may be limited on LAN HTTP.';
  setStatus(status, `${message}${secureHint}`);
}

async function copyInvite(message = 'Invite copied.') {
  const invite = inviteInput.value.trim();
  if (!invite) {
    setStatus(status, 'No invite is ready yet.');
    return false;
  }

  selectInvite();

  try {
    if (navigator.clipboard?.writeText && window.isSecureContext) {
      await navigator.clipboard.writeText(invite);
      setStatus(status, message);
      return true;
    }
  } catch (error) {
    setStatus(status, `Clipboard blocked: ${error.message}. The invite text is selected.`);
    return false;
  }

  try {
    if (document.execCommand('copy')) {
      setStatus(status, message);
      return true;
    }
  } catch {
  }

  setStatus(status, 'Clipboard is blocked by the Quest browser. The invite text is selected.');
  return false;
}

function openInCurrentTab(url, message) {
  if (!url) {
    setStatus(status, 'No invite is ready yet.');
    return;
  }
  setStatus(status, message);
  location.href = url;
}

function messengerUrl() {
  return 'https://www.messenger.com/';
}

function facebookUrl() {
  const invite = inviteInput.value.trim();
  const url = new URL('https://www.facebook.com/sharer/sharer.php');
  if (invite) {
    url.searchParams.set('u', invite);
  }
  return url.toString();
}

async function closeSharePage() {
  const token = controlToken();
  if (token) {
    try {
      const response = await fetch('/api/driver-room/close-share-page', {
        method: 'POST',
        headers: { 'content-type': 'application/json' },
        body: JSON.stringify({ token }),
      });
      const result = await response.json().catch(() => ({}));
      if (response.ok && result.ok) {
        setStatus(status, 'Returning to Quest Link.');
        return;
      }
      setStatus(status, result.error ?? 'Could not return to Quest Link from the PC.');
    } catch (error) {
      setStatus(status, `Close request failed: ${error.message}`);
    }
  }

  window.close();
  setTimeout(() => {
    setStatus(status, 'Use the Quest app switcher to reopen Quest Link if this tab stays open.');
  }, 300);
}

inviteInput.value = inviteUrl();
openInviteLink.href = inviteInput.value || '#';
openInviteLink.toggleAttribute('aria-disabled', !inviteInput.value);
setReadyStatus(inviteInput.value ? 'Invite ready.' : 'No invite was supplied.');

shareButton.addEventListener('click', async () => {
  const invite = inviteInput.value.trim();
  if (!invite) {
    setStatus(status, 'No invite is ready yet.');
    return;
  }
  if (navigator.share && window.isSecureContext) {
    try {
      await navigator.share({
        title: 'CoDriverVR passenger invite',
        text: 'Join my RBR passenger VR room',
        url: invite,
      });
      setStatus(status, 'Invite shared.');
      return;
    } catch (error) {
      setStatus(status, `Share cancelled or blocked: ${error.message}`);
    }
  }
  await copyInvite('Share sheet unavailable; invite copied or selected.');
});

copyButton.addEventListener('click', () => copyInvite().catch((error) => setStatus(status, `Copy failed: ${error.message}`)));
messengerButton.addEventListener('click', () => {
  copyInvite('Invite copied. Opening Messenger.').finally(() => openInCurrentTab(messengerUrl(), 'Opening Messenger. The invite text is selected if copy was blocked.'));
});
facebookButton.addEventListener('click', () => {
  copyInvite('Invite copied. Opening Facebook.').finally(() => openInCurrentTab(facebookUrl(), 'Opening Facebook sharing. The invite text is selected if copy was blocked.'));
});
closePageButton.addEventListener('click', () => closeSharePage());
