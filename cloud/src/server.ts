import express from 'express';
import OpusScript from 'opusscript';
import { WebSocket, WebSocketServer } from 'ws';
import type { IncomingMessage } from 'node:http';

const port = Number(process.env.PORT ?? 8080);

const app = express();
app.get('/healthz', (_request, response) => response.status(200).json({ ok: true }));
const server = app.listen(port, () => console.info(`pendant relay listening on ${port}`));
const sockets = new WebSocketServer({ noServer: true, maxPayload: 32 * 1024 });
server.on('upgrade', (request, socket, head) => {
  if (request.url?.split('?')[0] !== '/v1/voice') return socket.destroy();
  sockets.handleUpgrade(request, socket, head, (client) => sockets.emit('connection', client, request));
});

type ClientEvent =
  | { type: 'start'; sarvamApiKey: string; voice: string; language: string }
  | { type: 'mic_opus'; timestampMs: number; audio: string }
  | { type: 'finish' };

type SarvamSttMessage = { type?: string; data?: { transcript?: string; text?: string; message?: string } };

class VoiceSession {
  private authenticated = false;
  private closed = false;
  private stt?: WebSocket;
  private tts?: WebSocket;
  private readonly micDecoder = new OpusScript(16000, 1, OpusScript.Application.VOIP);
  private readonly speakerEncoder = new OpusScript(24000, 1, OpusScript.Application.VOIP);
  private speakerRemainder = Buffer.alloc(0);
  private answerBuffer = '';
  private startedReply = false;
  private readonly sentTranscript = new Set<string>();

  constructor(private readonly client: WebSocket, private readonly request: IncomingMessage) {}

  async accept(raw: WebSocket.RawData): Promise<void> {
    const event = JSON.parse(raw.toString()) as ClientEvent;
    if (!this.authenticated) {
      if (event.type !== 'start') throw new Error('The first message must start a session');
      await this.start(event);
      return;
    }
    if (event.type === 'mic_opus') this.forwardMic(event);
    if (event.type === 'finish') this.stt?.send(JSON.stringify({ type: 'flush' }));
  }

  close(): void {
    this.closed = true;
    this.stt?.close();
    this.tts?.close();
    this.stt = undefined;
    this.tts = undefined;
    this.upstreamKey = undefined;
    this.answerBuffer = '';
    this.speakerRemainder = Buffer.alloc(0);
  }

  private async start(event: Extract<ClientEvent, { type: 'start' }>): Promise<void> {
    if (!event.sarvamApiKey.startsWith('sk_')) throw new Error('Invalid Sarvam API key format');
    if (!['ratan', 'priya', 'ishita', 'shubh', 'ritu', 'mani'].includes(event.voice)) throw new Error('Unsupported voice');
    this.authenticated = true;
    this.upstreamKey = event.sarvamApiKey;
    this.send({ type: 'status', phase: 'listening', message: 'Sarvam Saaras v3 is listening' });
    this.openStt(event.sarvamApiKey, event.language);
    this.openTts(event.sarvamApiKey, event.voice, event.language);
    // Deliberately allow references to the user-provided key to fall out of
    // scope after the upstream sockets are opened. It is never logged/stored.
  }

  private openStt(apiKey: string, language: string): void {
    const query = new URLSearchParams({
      'language-code': language,
      model: 'saaras:v3', mode: 'codemix', sample_rate: '16000', input_audio_codec: 'pcm_s16le',
      high_vad_sensitivity: 'true', vad_signals: 'true', flush_signal: 'true',
    });
    this.stt = new WebSocket(`wss://api.sarvam.ai/speech-to-text/ws?${query}`, { headers: { 'api-subscription-key': apiKey } });
    this.stt.on('message', (data) => this.onStt(data.toString()));
    this.stt.on('error', () => this.fail('Sarvam transcription connection failed'));
    this.stt.on('close', (code) => { if (!this.closed && code !== 1000) this.fail('Sarvam transcription disconnected'); });
  }

  private openTts(apiKey: string, voice: string, language: string): void {
    this.tts = new WebSocket('wss://api.sarvam.ai/text-to-speech/ws?model=bulbul:v3&send_completion_event=true', { headers: { 'api-subscription-key': apiKey } });
    this.tts.on('open', () => this.tts?.send(JSON.stringify({ type: 'config', data: {
      speaker: voice, target_language_code: language, pace: 1.0, min_buffer_size: 32, max_chunk_length: 180,
      output_audio_codec: 'linear16', speech_sample_rate: 24000, temperature: 0.6,
    } })));
    this.tts.on('message', (data) => this.onTts(data.toString()));
    this.tts.on('error', () => this.fail('Sarvam speech connection failed'));
  }

  private forwardMic(event: Extract<ClientEvent, { type: 'mic_opus' }>): void {
    if (this.stt?.readyState !== WebSocket.OPEN) return;
    const encoded = Buffer.from(event.audio, 'base64');
    if (encoded.length === 0 || encoded.length > 192) return;
    const pcm = this.micDecoder.decode(encoded);
    this.stt.send(JSON.stringify({ audio: { data: pcm.toString('base64'), sample_rate: 16000, encoding: 'pcm_s16le' } }));
  }

  private onStt(raw: string): void {
    let message: SarvamSttMessage;
    try { message = JSON.parse(raw) as SarvamSttMessage; } catch { return; }
    const transcript = message.data?.transcript ?? message.data?.text;
    if (!transcript?.trim() || this.sentTranscript.has(transcript)) return;
    this.sentTranscript.add(transcript);
    this.send({ type: 'transcript', text: transcript, final: true });
    void this.answer(transcript);
  }

  private async answer(transcript: string): Promise<void> {
    if (this.startedReply) return;
    this.startedReply = true;
    this.send({ type: 'status', phase: 'thinking', message: 'Sarvam-30B is composing a reply' });
    try {
      const response = await fetch('https://api.sarvam.ai/v1/chat/completions', {
        method: 'POST',
        headers: { 'content-type': 'application/json', authorization: `Bearer ${this.keyForUpstream()}` },
        body: JSON.stringify({
          model: 'sarvam-30b', stream: true, reasoning_effort: null, temperature: 0.3, max_tokens: 180,
          messages: [
            { role: 'system', content: 'You are Pendant, a concise warm voice assistant. Reply in natural English or Hinglish. Speak in short sentences and never reveal hidden reasoning.' },
            { role: 'user', content: transcript },
          ],
        }),
      });
      if (!response.ok || !response.body) throw new Error(`LLM HTTP ${response.status}`);
      const reader = response.body.getReader();
      const decoder = new TextDecoder();
      let pending = '';
      for (;;) {
        const { done, value } = await reader.read();
        if (done) break;
        pending += decoder.decode(value, { stream: true });
        const lines = pending.split('\n');
        pending = lines.pop() ?? '';
        for (const line of lines) this.acceptSse(line);
      }
      if (this.answerBuffer.trim()) this.enqueueTts(this.answerBuffer.trim());
      this.tts?.send(JSON.stringify({ type: 'flush' }));
    } catch {
      this.fail('Sarvam assistant is temporarily unavailable');
    }
  }

  // The session key is held only for the active socket and cleared on close.
  private upstreamKey?: string;
  private keyForUpstream(): string {
    if (!this.upstreamKey) throw new Error('Voice session key unavailable');
    return this.upstreamKey;
  }

  private acceptSse(line: string): void {
    if (!line.startsWith('data:')) return;
    const value = line.slice(5).trim();
    if (!value || value === '[DONE]') return;
    try {
      const json = JSON.parse(value) as { choices?: Array<{ delta?: { content?: string } }> };
      const content = json.choices?.[0]?.delta?.content;
      if (!content) return;
      this.answerBuffer += content;
      const boundary = this.answerBuffer.search(/[.!?]\s|\n/);
      if (boundary >= 0 || this.answerBuffer.length >= 150) {
        const segment = this.answerBuffer.slice(0, boundary >= 0 ? boundary + 1 : this.answerBuffer.length).trim();
        this.answerBuffer = this.answerBuffer.slice(boundary >= 0 ? boundary + 1 : this.answerBuffer.length);
        if (segment) this.enqueueTts(segment);
      }
    } catch { /* malformed SSE chunks are ignored */ }
  }

  private enqueueTts(text: string): void {
    if (this.tts?.readyState !== WebSocket.OPEN) return;
    this.send({ type: 'status', phase: 'speaking', message: 'Bulbul v3 is speaking' });
    this.tts.send(JSON.stringify({ type: 'text', data: { text } }));
  }

  private onTts(raw: string): void {
    try {
      const message = JSON.parse(raw) as { type?: string; data?: { audio?: string } };
      if (message.type !== 'audio' || !message.data?.audio) return;
      this.emitPcm(Buffer.from(message.data.audio, 'base64'));
    } catch { /* status and completion messages are not audio */ }
  }

  private emitPcm(chunk: Buffer): void {
    this.speakerRemainder = Buffer.concat([this.speakerRemainder, chunk]);
    const bytesPerFrame = 480 * 2;
    while (this.speakerRemainder.length >= bytesPerFrame) {
      const pcm = this.speakerRemainder.subarray(0, bytesPerFrame);
      this.speakerRemainder = this.speakerRemainder.subarray(bytesPerFrame);
      let sum = 0;
      for (let index = 0; index < pcm.length; index += 2) sum += Math.abs(pcm.readInt16LE(index));
      const mouthLevel = Math.min(255, Math.round((sum / 480) * 255 / 9000));
      const opus = this.speakerEncoder.encode(pcm, 480);
      this.send({ type: 'speaker_opus', audio: opus.toString('base64'), timestampMs: Date.now(), mouthLevel });
    }
  }

  private send(event: object): void { if (!this.closed && this.client.readyState === WebSocket.OPEN) this.client.send(JSON.stringify(event)); }
  private fail(message: string): void { if (!this.closed) this.send({ type: 'error', message }); }
}

sockets.on('connection', (client, request) => {
  const session = new VoiceSession(client, request);
  client.on('message', (raw) => void session.accept(raw).catch(() => {
    client.send(JSON.stringify({ type: 'error', message: 'Cloud Voice setup failed' }));
    client.close(1008, 'invalid session');
  }));
  client.on('close', () => session.close());
  client.on('error', () => session.close());
});
