/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#if !defined(nsOggReader_h_)
#define nsOggReader_h_

#include <ogg/ogg.h>
#include <theora/theoradec.h>
#ifdef MOZ_TREMOR
#include <tremor/ivorbiscodec.h>
#else
#include <vorbis/codec.h>
#endif
#include "nsBuiltinDecoderReader.h"
#include "nsOggCodecState.h"
#include "VideoUtils.h"


class nsMediaDecoder;
class nsTimeRanges;

class nsOggReader : public nsBuiltinDecoderReader
{
public:
  nsOggReader(nsBuiltinDecoder* aDecoder);
  ~nsOggReader();

  virtual nsresult Init(nsBuiltinDecoderReader* aCloneDonor);
  virtual nsresult ResetDecode();
  virtual bool DecodeAudioData();

  // If the Theora granulepos has not been captured, it may read several packets
  // until one with a granulepos has been captured, to ensure that all packets
  // read have valid time info.  
  virtual bool DecodeVideoFrame(bool &aKeyframeSkip,
                                  int64_t aTimeThreshold);

  virtual bool HasAudio() {
    return (mVorbisState != 0 && mVorbisState->mActive) ||
           (mOpusState != 0 && mOpusState->mActive);
  }

  virtual bool HasVideo() {
    return mTheoraState != 0 && mTheoraState->mActive;
  }

  virtual nsresult ReadMetadata(nsVideoInfo* aInfo,
                                nsHTMLMediaElement::MetadataTags** aTags);
  virtual nsresult Seek(int64_t aTime, int64_t aStartTime, int64_t aEndTime, int64_t aCurrentTime);
  virtual nsresult GetBuffered(nsTimeRanges* aBuffered, int64_t aStartTime);

  // We use bisection to seek in buffered range.
  virtual bool IsSeekableInBufferedRanges() {
    return true;
  }

private:

  // Specialized Reset() method to signal if the seek is
  // to the start of the stream.
  nsresult ResetDecode(bool start);

  bool HasSkeleton() {
    return mSkeletonState != 0 && mSkeletonState->mActive;
  }

  // Seeks to the keyframe preceeding the target time using available
  // keyframe indexes.
  enum IndexedSeekResult {
    SEEK_OK,          // Success.
    SEEK_INDEX_FAIL,  // Failure due to no index, or invalid index.
    SEEK_FATAL_ERROR  // Error returned by a stream operation.
  };
  IndexedSeekResult SeekToKeyframeUsingIndex(int64_t aTarget);

  // Rolls back a seek-using-index attempt, returning a failure error code.
  IndexedSeekResult RollbackIndexedSeek(int64_t aOffset);

  // Represents a section of contiguous media, with a start and end offset,
  // and the timestamps of the start and end of that range, that is cached.
  // Used to denote the extremities of a range in which we can seek quickly
  // (because it's cached).
  class SeekRange {
  public:
    SeekRange()
      : mOffsetStart(0),
        mOffsetEnd(0),
        mTimeStart(0),
        mTimeEnd(0)
    {}

    SeekRange(int64_t aOffsetStart,
              int64_t aOffsetEnd,
              int64_t aTimeStart,
              int64_t aTimeEnd)
      : mOffsetStart(aOffsetStart),
        mOffsetEnd(aOffsetEnd),
        mTimeStart(aTimeStart),
        mTimeEnd(aTimeEnd)
    {}

    bool IsNull() const {
      return mOffsetStart == 0 &&
             mOffsetEnd == 0 &&
             mTimeStart == 0 &&
             mTimeEnd == 0;
    }

    int64_t mOffsetStart, mOffsetEnd; // in bytes.
    int64_t mTimeStart, mTimeEnd; // in usecs.
  };

  // Seeks to aTarget usecs in the buffered range aRange using bisection search,
  // or to the keyframe prior to aTarget if we have video. aAdjustedTarget is
  // an adjusted version of the target used to account for Opus pre-roll, if
  // necessary. aStartTime must be the presentation time at the start of media,
  // and aEndTime the time at end of media. aRanges must be the time/byte ranges
  // buffered in the media cache as per GetSeekRanges().
  nsresult SeekInBufferedRange(int64_t aTarget,
                               int64_t aAdjustedTarget,
                               int64_t aStartTime,
                               int64_t aEndTime,
                               const nsTArray<SeekRange>& aRanges,
                               const SeekRange& aRange);

  // Seeks to before aTarget usecs in media using bisection search. If the media
  // has video, this will seek to before the keyframe required to render the
  // media at aTarget. Will use aRanges in order to narrow the bisection
  // search space. aStartTime must be the presentation time at the start of
  // media, and aEndTime the time at end of media. aRanges must be the time/byte
  // ranges buffered in the media cache as per GetSeekRanges().
  nsresult SeekInUnbuffered(int64_t aTarget,
                            int64_t aStartTime,
                            int64_t aEndTime,
                            const nsTArray<SeekRange>& aRanges);

  // Get the end time of aEndOffset. This is the playback position we'd reach
  // after playback finished at aEndOffset.
  int64_t RangeEndTime(int64_t aEndOffset);

  // Get the end time of aEndOffset, without reading before aStartOffset.
  // This is the playback position we'd reach after playback finished at
  // aEndOffset. If bool aCachedDataOnly is true, then we'll only read
  // from data which is cached in the media cached, otherwise we'll do
  // regular blocking reads from the media stream. If bool aCachedDataOnly
  // is true, this can safely be called on the main thread, otherwise it
  // must be called on the state machine thread.
  int64_t RangeEndTime(int64_t aStartOffset,
                       int64_t aEndOffset,
                       bool aCachedDataOnly);

  // Get the start time of the range beginning at aOffset. This is the start
  // time of the first frame and or audio sample we'd be able to play if we
  // started playback at aOffset.
  int64_t RangeStartTime(int64_t aOffset);

  // Performs a seek bisection to move the media stream's read cursor to the
  // last ogg page boundary which has end time before aTarget usecs on both the
  // Theora and Vorbis bitstreams. Limits its search to data inside aRange;
  // i.e. it will only read inside of the aRange's start and end offsets.
  // aFuzz is the number of usecs of leniency we'll allow; we'll terminate the
  // seek when we land in the range (aTime - aFuzz, aTime) usecs.
  nsresult SeekBisection(int64_t aTarget,
                         const SeekRange& aRange,
                         uint32_t aFuzz);

  // Returns true if the serial number is for a stream we encountered
  // while reading metadata. Call on the main thread only.
  bool IsKnownStream(uint32_t aSerial);

  // Fills aRanges with SeekRanges denoting the sections of the media which
  // have been downloaded and are stored in the media cache. The reader
  // monitor must must be held with exactly one lock count. The MediaResource
  // must be pinned while calling this.
  nsresult GetSeekRanges(nsTArray<SeekRange>& aRanges);

  // Returns the range in which you should perform a seek bisection if
  // you wish to seek to aTarget usecs, given the known (buffered) byte ranges
  // in aRanges. If aExact is true, we only return an exact copy of a
  // range in which aTarget lies, or a null range if aTarget isn't contained
  // in any of the (buffered) ranges. Otherwise, when aExact is false,
  // we'll construct the smallest possible range we can, based on the times
  // and byte offsets known in aRanges. We can then use this to minimize our
  // bisection's search space when the target isn't in a known buffered range.
  SeekRange SelectSeekRange(const nsTArray<SeekRange>& aRanges,
                            int64_t aTarget,
                            int64_t aStartTime,
                            int64_t aEndTime,
                            bool aExact);
private:

  // Decodes a packet of Vorbis data, and inserts its samples into the 
  // audio queue.
  nsresult DecodeVorbis(ogg_packet* aPacket);

  // Decodes a packet of Opus data, and inserts its samples into the
  // audio queue.
  nsresult DecodeOpus(ogg_packet* aPacket);

  // Decodes a packet of Theora data, and inserts its frame into the
  // video queue. May return NS_ERROR_OUT_OF_MEMORY. Caller must have obtained
  // the reader's monitor. aTimeThreshold is the current playback position
  // in media time in microseconds. Frames with an end time before this will
  // not be enqueued.
  nsresult DecodeTheora(ogg_packet* aPacket, int64_t aTimeThreshold);

  // Read a page of data from the Ogg file. Returns the offset of the start
  // of the page, or -1 if the page read failed.
  int64_t ReadOggPage(ogg_page* aPage);

  // Reads and decodes header packets for aState, until either header decode
  // fails, or is complete. Initializes the codec state before returning.
  // Returns true if reading headers and initializtion of the stream
  // succeeds.
  bool ReadHeaders(nsOggCodecState* aState);

  // Returns the next Ogg packet for an bitstream/codec state. Returns a
  // pointer to an ogg_packet on success, or nullptr if the read failed.
  // The caller is responsible for deleting the packet and its |packet| field.
  ogg_packet* NextOggPacket(nsOggCodecState* aCodecState);

  // Fills aTracks with the serial numbers of each active stream, for use by
  // various nsSkeletonState functions.
  void BuildSerialList(nsTArray<uint32_t>& aTracks);

  // Maps Ogg serialnos to nsOggStreams.
  nsClassHashtable<nsUint32HashKey, nsOggCodecState> mCodecStates;

  // Array of serial numbers of streams that were encountered during
  // initial metadata load. Written on state machine thread during
  // metadata loading and read on the main thread only after metadata
  // is loaded.
  nsAutoTArray<uint32_t,4> mKnownStreams;

  // Decode state of the Theora bitstream we're decoding, if we have video.
  nsTheoraState* mTheoraState;

  // Decode state of the Vorbis bitstream we're decoding, if we have audio.
  nsVorbisState* mVorbisState;

  // Decode state of the Opus bitstream we're decoding, if we have one.
  nsOpusState *mOpusState;

  // Represents the user pref media.opus.enabled at the time our
  // contructor was called. We can't check it dynamically because
  // we're not on the main thread;
  bool mOpusEnabled;

  // Decode state of the Skeleton bitstream.
  nsSkeletonState* mSkeletonState;

  // Ogg decoding state.
  ogg_sync_state mOggState;

  // Vorbis/Opus/Theora data used to compute timestamps. This is written on the
  // decoder thread and read on the main thread. All reading on the main
  // thread must be done after metadataloaded. We can't use the existing
  // data in the codec states due to threading issues. You must check the
  // associated mTheoraState or mVorbisState pointer is non-null before
  // using this codec data.
  uint32_t mVorbisSerial;
  uint32_t mOpusSerial;
  uint32_t mTheoraSerial;
  vorbis_info mVorbisInfo;
  int mOpusPreSkip;
  th_info mTheoraInfo;

  // The offset of the end of the last page we've read, or the start of
  // the page we're about to read.
  int64_t mPageOffset;

  // The picture region inside Theora frame to be displayed, if we have
  // a Theora video track.
  nsIntRect mPicture;
};

#endif
