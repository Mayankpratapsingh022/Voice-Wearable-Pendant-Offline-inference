declare module 'opusscript' {
  class OpusScript {
    static readonly Application: { readonly VOIP: number };
    constructor(sampleRate: number, channels: number, application: number);
    decode(packet: Buffer): Buffer;
    encode(pcm: Buffer, frameSize: number): Buffer;
  }
  export default OpusScript;
}
