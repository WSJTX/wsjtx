#ifndef NETWORK_MESSAGE_HPP__
#define NETWORK_MESSAGE_HPP__

/*
 * WSJT-X Message Formats
 * ======================
 *
 * All messages are written or  read using the QDataStream derivatives
 * defined  below, note  that we  are using  the default  for floating
 * point precision  which means all  are double precision  i.e. 64-bit
 * IEEE format.
 *
 *  Message is big endian format
 *
 *   Header format:
 *
 *      32-bit unsigned integer magic number 0xadbccbda
 *      32-bit unsigned integer schema number
 *
 *   Payload format:
 *
 *      As per the QDataStream format,  see below for version used and
 *      here:
 *
 *        http://doc.qt.io/qt-5/datastreamformat.html
 *
 *      for the  serialization details for  each type, at the  time of
 *      writing the above document is for Qt_5_0 format which is buggy
 *      so we use Qt_5_4 format, differences are:
 *
 *      QDateTime:
 *           QDate      qint64    Julian day number
 *           QTime      quint32   Milli-seconds since midnight
 *           timespec   quint8    0=local, 1=UTC, 2=Offset from UTC
 *                                                 (seconds)
 *                                3=time zone
 *           offset     qint32    only present if timespec=2
 *           timezone   several-fields only present if timespec=3
 *
 *      we  will avoid  using  QDateTime fields  with  time zones  for
 *      simplicity.
 *
 * Type utf8  is a  utf-8 byte  string formatted  as a  QByteArray for
 * serialization purposes  (currently a quint32 size  followed by size
 * bytes, no terminator is present or counted).
 *
 * The QDataStream  format document linked  above is not  complete for
 * the QByteArray serialization  format, it is similar  to the QString
 * serialization  format  in  that  it  differentiates  between  empty
 * strings  and null  strings. Empty  strings  have a  length of  zero
 * whereas null strings have a length field of 0xffffffff.
 *
 *
 * Schema Negotiation
 * ------------------
 *
 * The NetworkMessage::Builder  class specifies a schema  number which
 * may be  incremented from time to  time. It represents a  version of
 * the underlying encoding schemes used to store data items. Since the
 * underlying  encoding  is   defined  by  the  Qt   project  in  it's
 * QDataStream  stream operators,  it  is essential  that clients  and
 * servers  of  this protocol  can  agree  on  a common  scheme.   The
 * NetworkMessage  utility classes  below exchange  the schema  number
 * actually used.  The handling of  the schema is backwards compatible
 * to  an  extent,  so  long   as  clients  and  servers  are  written
 * correctly. For  example a server  written to any  particular schema
 * version can communicate with a client written to a later schema.
 *
 * Schema Version 1:- this schema used the QDataStream::Qt_5_0 version
 *  which is broken.
 *
 * Schema Version 2:- this schema uses the QDataStream::Qt_5_2 version.
 *
 * Schema Version 3:- this schema uses the QDataStream::Qt_5_4 version.
 *
 *
 * Backward Compatibility
 * ----------------------
 *
 * It  is important  that  applications developed  at different  times
 * remain  compatible  with this  protocol  and  with older  or  newer
 * versions  of   WSJT-X.   This  is  achieved   by  both  third-party
 * applications and WSJT-X honouring two basic rules.
 *
 * 1. New  message types may be  added to the protocol  in the future,
 *    third-party applications  and WSJT-X  shall ignore  silently any
 *    message types they do not recognize.
 *
 * 2. New  fields may be  added to  existing message types,  they will
 *    always be added to the end of the existing fields and the number
 *    and type  of existing fields shall  not change. If a  field type
 *    must be  changed; a  new field  will be  added and  the existing
 *    field  will  remain. The  originator  of  such a  message  shall
 *    populate   both  the   new   and  old   field  with   reasonable
 *    values.  Third-party   applications  and  WSJT-X   shall  ignore
 *    silently any extra  data received in datagrams  after the fields
 *    they know about.
 *
 * Note  that these  rules are  unrelated to  the schema  number above
 * whose purpose is to distinguish between non-compatible encodings of
 * field data  types. New message  types and extra fields  in existing
 * messages can and will be added without any change in schema number.
 *
 *
 * Message Types
 * -------------
 *
 * Message       Direction Value                  Type
 * ------------- --------- ---------------------- -----------
 * Heartbeat     Out/In    0                      quint32
 *                         Id (unique key)        utf8
 *                         Maximum schema number  quint32
 *                         version                utf8
 *                         revision               utf8
 *
 *    The heartbeat  message shall be  sent on a periodic  basis every
 *    NetworkMessage::pulse   seconds   (see    below),   the   WSJT-X
 *    application  does  that  using the  MessageClient  class.   This
 *    message is intended to be used by servers to detect the presence
 *    of a  client and also  the unexpected disappearance of  a client
 *    and  by clients  to learn  the schema  negotiated by  the server
 *    after it receives  the initial heartbeat message  from a client.
 *    The message_aggregator reference server does just that using the
 *    MessageServer class. Upon  initial startup a client  must send a
 *    heartbeat message as soon as  is practical, this message is used
 *    to negotiate the maximum schema  number common to the client and
 *    server. Note  that the  server may  not be  able to  support the
 *    client's  requested maximum  schema  number, in  which case  the
 *    first  message received  from the  server will  specify a  lower
 *    schema number (never a higher one  as that is not allowed). If a
 *    server replies  with a lower  schema number then no  higher than
 *    that number shall be used for all further outgoing messages from
 *    either clients or the server itself.
 *
 *    Note: the  "Maximum schema number"  field was introduced  at the
 *    same time as schema 3, therefore servers and clients must assume
 *    schema 2 is the highest schema number supported if the Heartbeat
 *    message does not contain the "Maximum schema number" field.
 *
 *
 * Status        Out       1                      quint32
 *                         Id (unique key)        utf8
 *                         Dial Frequency (Hz)    quint64
 *                         Mode                   utf8
 *                         DX call                utf8
 *                         Report                 utf8
 *                         Tx Mode                utf8
 *                         Tx Enabled             bool
 *                         Transmitting           bool
 *                         Decoding               bool
 *                         Rx DF                  quint32
 *                         Tx DF                  quint32
 *                         DE call                utf8
 *                         DE grid                utf8
 *                         DX grid                utf8
 *                         Tx Watchdog            bool
 *                         Sub-mode               utf8
 *                         Fast mode              bool
 *                         Special Operation Mode quint8
 *                         Frequency Tolerance    quint32
 *                         T/R Period             quint32
 *                         Configuration Name     utf8
 *
 *    WSJT-X  sends this  status message  when various  internal state
 *    changes to allow the server to  track the relevant state of each
 *    client without the need for  polling commands. The current state
 *    changes that generate status messages are:
 *
 *      Application start up,
 *      "Enable Tx" button status changes,
 *      dial frequency changes,
 *      changes to the "DX Call" field,
 *      operating mode, sub-mode or fast mode changes,
 *      transmit mode changed (in dual JT9+JT65 mode),
 *      changes to the "Rpt" spinner,
 *      after an old decodes replay sequence (see Replay below),
 *      when switching between Tx and Rx mode,
 *      at the start and end of decoding,
 *      when the Rx DF changes,
 *      when the Tx DF changes,
 *      when settings are exited,
 *      when the DX call or grid changes,
 *      when the Tx watchdog is set or reset,
 *      when the frequency tolerance is changed,
 *      when the T/R period is changed,
 *      when the configuration name changes.
 *
 *    The Special operation mode is  an enumeration that indicates the
 *    setting  selected  in  the  WSJT-X  "Settings->Advanced->Special
 *    operating activity" panel. The values are as follows:
 *
 *       0 -> NONE
 *       1 -> NA VHF
 *       2 -> EU VHF
 *       3 -> FIELD DAY
 *       4 -> RTTY RU
 *       5 -> FOX
 *       6 -> HOUND
 *
 *    The Frequency Tolerance  and T/R period fields may  have a value
 *    of  the maximum  quint32 value  which implies  the field  is not
 *    applicable.
 *
 *
 * Decode        Out       2                      quint32
 *                         Id (unique key)        utf8
 *                         New                    bool
 *                         Time                   QTime
 *                         snr                    qint32
 *                         Delta time (S)         float (serialized as double)
 *                         Delta frequency (Hz)   quint32
 *                         Mode                   utf8
 *                         Message                utf8
 *                         Low confidence         bool
 *                         Off air                bool
 *
 *      The decode message is sent when  a new decode is completed, in
 *      this case the 'New' field is true. It is also used in response
 *      to  a "Replay"  message where  each  old decode  in the  "Band
 *      activity" window, that  has not been erased, is  sent in order
 *      as a one of these messages  with the 'New' field set to false.
 *      See  the "Replay"  message below  for details  of usage.   Low
 *      confidence decodes are flagged  in protocols where the decoder
 *      has knows that  a decode has a higher  than normal probability
 *      of  being  false, they  should  not  be reported  on  publicly
 *      accessible services  without some attached warning  or further
 *      validation. Off air decodes are those that result from playing
 *      back a .WAV file.
 *
 *
 * Clear         Out/In    3                      quint32
 *                         Id (unique key)        utf8
 *                         Window                 quint8 (In only)
 *
 *      This message is  send when all prior "Decode"  messages in the
 *      "Band Activity"  window have been discarded  and therefore are
 *      no long available for actioning  with a "Reply" message. It is
 *      sent when the user erases  the "Band activity" window and when
 *      WSJT-X  closes down  normally. The  server should  discard all
 *      decode messages upon receipt of this message.
 *
 *      It may  also be  sent to  a WSJT-X instance  in which  case it
 *      clears one or  both of the "Band Activity"  and "Rx Frequency"
 *      windows.  The Window  argument  can be  one  of the  following
 *      values:
 *
 *         0  - clear the "Band Activity" window (default)
 *         1  - clear the "Rx Frequency" window
 *         2  - clear both "Band Activity" and "Rx Frequency" windows
 *
 *
 * Reply         In        4                      quint32
 *                         Id (target unique key) utf8
 *                         Time                   QTime
 *                         snr                    qint32
 *                         Delta time (S)         float (serialized as double)
 *                         Delta frequency (Hz)   quint32
 *                         Mode                   utf8
 *                         Message                utf8
 *                         Low confidence         bool
 *                         Modifiers              quint8
 *
 *      In order for a server  to provide a useful cooperative service
 *      to WSJT-X it  is possible for it to initiate  a QSO by sending
 *      this message to a client. WSJT-X filters this message and only
 *      acts upon it  if the message exactly describes  a prior decode
 *      and that decode  is a CQ or QRZ message.   The action taken is
 *      exactly equivalent to the user  double clicking the message in
 *      the "Band activity" window. The  intent of this message is for
 *      servers to be able to provide an advanced look up of potential
 *      QSO partners, for example determining if they have been worked
 *      before  or if  working them  may advance  some objective  like
 *      award progress.  The  intention is not to  provide a secondary
 *      user  interface for  WSJT-X,  it is  expected  that after  QSO
 *      initiation the rest  of the QSO is carried  out manually using
 *      the normal WSJT-X user interface.
 *
 *      The  Modifiers   field  allows  the  equivalent   of  keyboard
 *      modifiers to be sent "as if" those modifier keys where pressed
 *      while  double-clicking  the  specified  decoded  message.  The
 *      modifier values (hexadecimal) are as follows:
 *
 *          no modifier     0x00
 *          SHIFT           0x02
 *          CTRL            0x04  CMD on Mac
 *          ALT             0x08
 *          META            0x10  Windows key on MS Windows
 *          KEYPAD          0x20  Keypad or arrows
 *          Group switch    0x40  X11 only
 *
 *
 * QSO Logged    Out       5                      quint32
 *                         Id (unique key)        utf8
 *                         Date & Time Off        QDateTime
 *                         DX call                utf8
 *                         DX grid                utf8
 *                         Tx frequency (Hz)      quint64
 *                         Mode                   utf8
 *                         Report sent            utf8
 *                         Report received        utf8
 *                         Tx power               utf8
 *                         Comments               utf8
 *                         Name                   utf8
 *                         Date & Time On         QDateTime
 *                         Operator call          utf8
 *                         My call                utf8
 *                         My grid                utf8
 *                         Exchange sent          utf8
 *                         Exchange received      utf8
 *
 *      The  QSO logged  message is  sent  to the  server(s) when  the
 *      WSJT-X user accepts the "Log  QSO" dialog by clicking the "OK"
 *      button.
 *
 *
 * Close         Out/In    6                      quint32
 *                         Id (unique key)        utf8
 *
 *      Close is  sent by  a client immediately  prior to  it shutting
 *      down gracefully. When sent by  a server it requests the target
 *      client to close down gracefully.
 *
 *
 * Replay        In        7                      quint32
 *                         Id (unique key)        utf8
 *
 *      When a server starts it may  be useful for it to determine the
 *      state  of preexisting  clients. Sending  this message  to each
 *      client as it is discovered  will cause that client (WSJT-X) to
 *      send a "Decode" message for each decode currently in its "Band
 *      activity"  window. Each  "Decode" message  sent will  have the
 *      "New" flag set to false so that they can be distinguished from
 *      new decodes. After  all the old decodes have  been broadcast a
 *      "Status" message  is also broadcast.  If the server  wishes to
 *      determine  the  status  of  a newly  discovered  client;  this
 *      message should be used.
 *
 *
 * Halt Tx       In        8
 *                         Id (unique key)        utf8
 *                         Auto Tx Only           bool
 *
 *      The server may stop a client from transmitting messages either
 *      immediately or at  the end of the  current transmission period
 *      using this message.
 *
 *
 * Free Text     In        9
 *                         Id (unique key)        utf8
 *                         Text                   utf8
 *                         Send                   bool
 *
 *      This message  allows the server  to set the current  free text
 *      message content. Sending this  message with a non-empty "Text"
 *      field is equivalent to typing  a new message (old contents are
 *      discarded) in to  the WSJT-X free text message  field or "Tx5"
 *      field (both  are updated) and if  the "Send" flag is  set then
 *      clicking the "Now" radio button for the "Tx5" field if tab one
 *      is current or clicking the "Free  msg" radio button if tab two
 *      is current.
 *
 *      It is the responsibility of the  sender to limit the length of
 *      the  message   text  and   to  limit   it  to   legal  message
 *      characters. Despite this,  it may be difficult  for the sender
 *      to determine the maximum message length without reimplementing
 *      the complete message encoding protocol. Because of this is may
 *      be better  to allow any  reasonable message length and  to let
 *      the WSJT-X application encode and possibly truncate the actual
 *      on-air message.
 *
 *      If the  message text is  empty the  meaning of the  message is
 *      refined  to send  the  current free  text  unchanged when  the
 *      "Send" flag is set or to  clear the current free text when the
 *      "Send" flag is  unset.  Note that this API does  not include a
 *      command to  determine the  contents of  the current  free text
 *      message.
 *
 *
 * WSPRDecode    Out       10                     quint32
 *                         Id (unique key)        utf8
 *                         New                    bool
 *                         Time                   QTime
 *                         snr                    qint32
 *                         Delta time (S)         float (serialized as double)
 *                         Frequency (Hz)         quint64
 *                         Drift (Hz)             qint32
 *                         Callsign               utf8
 *                         Grid                   utf8
 *                         Power (dBm)            qint32
 *                         Off air                bool
 *
 *      The decode message is sent when  a new decode is completed, in
 *      this case the 'New' field is true. It is also used in response
 *      to  a "Replay"  message where  each  old decode  in the  "Band
 *      activity" window, that  has not been erased, is  sent in order
 *      as  a one  of  these  messages with  the  'New'  field set  to
 *      false.  See   the  "Replay"  message  below   for  details  of
 *      usage. The off air field indicates that the decode was decoded
 *      from a played back recording.
 *
 *
 * Location       In       11
 *                         Id (unique key)        utf8
 *                         Location               utf8
 *
 *      This  message allows  the server  to set  the current  current
 *      geographical location  of operation. The supplied  location is
 *      not persistent but  is used as a  session lifetime replacement
 *      loction that overrides the Maidenhead  grid locater set in the
 *      application  settings.  The  intent  is to  allow an  external
 *      application  to  update  the  operating  location  dynamically
 *      during a mobile period of operation.
 *
 *      Currently  only Maidenhead  grid  squares  or sub-squares  are
 *      accepted, i.e.  4- or 6-digit  locators. Other formats  may be
 *      accepted in future.
 *
 *
 * Logged ADIF    Out      12                     quint32
 *                         Id (unique key)        utf8
 *                         ADIF text              utf8
 *
 *      The  logged ADIF  message is  sent to  the server(s)  when the
 *      WSJT-X user accepts the "Log  QSO" dialog by clicking the "OK"
 *      button. The  "ADIF text" field  consists of a valid  ADIF file
 *      such that  the WSJT-X  UDP header information  is encapsulated
 *      into a valid ADIF header. E.g.:
 *
 *          <magic-number><schema-number><type><id><32-bit-count>  # binary encoded fields
 *          # the remainder is the contents of the ADIF text field
 *          <adif_ver:5>3.0.7
 *          <programid:6>WSJT-X
 *          <EOH>
 *          ADIF log data fields ...<EOR>
 *
 *      Note that  receiving applications can treat  the whole message
 *      as a valid ADIF file with one record without special parsing.
 *
 *
 * Highlight Callsign In   13                     quint32
 *                         Id (unique key)        utf8
 *                         Callsign               utf8
 *                         Background Color       QColor
 *                         Foreground Color       QColor
 *                         Highlight last         bool
 *
 *      The server  may send  this message at  any time.   The message
 *      specifies  the background  and foreground  color that  will be
 *      used  to  highlight  the  specified callsign  in  the  decoded
 *      messages  printed  in  the  Band Activity  panel.  The  WSJT-X
 *      clients maintain a list of such instructions and apply them to
 *      all decoded  messages in the  band activity window.   To clear
 *      highlighting send an  invalid QColor value for  either or both
 *      of the background and foreground fields.
 *
 *      The "Highlight last"  field allows the sender  to request that
 *      the  last  instance  only  instead of  all  instances  of  the
 *      specified  call  be  highlighted  or  have  it's  highlighting
 *      cleared.
 *
 *
 * SwitchConfiguration  In 14                     quint32
 *                         Id (unique key)        utf8
 *                         Configuration Name     utf8
 *
 *      The server  may send  this message at  any time.   The message
 *      specifies the name of the  configuration to switch to. The new
 *      configuration must exist.
 *
 *
 * Configure      In       15                     quint32
 *                         Id (unique key)        utf8
 *                         Mode                   utf8
 *                         Frequency Tolerance    quint32
 *                         Submode                utf8
 *                         Fast Mode              bool
 *                         T/R Period             quint32
 *                         Rx DF                  quint32
 *                         DX Call                utf8
 *                         DX Grid                utf8
 *                         Generate Messages      bool
 *
 *      The server  may send  this message at  any time.   The message
 *      specifies  various  configuration  options.  For  utf8  string
 *      fields an empty value implies no change, for the quint32 Rx DF
 *      and  Frequency  Tolerance  fields the  maximum  quint32  value
 *      implies  no change.   Invalid or  unrecognized values  will be
 *      silently ignored.
 */

#include <QDataStream>

#include "pimpl_h.hpp"

class QIODevice;
class QByteArray;
class QString;

namespace NetworkMessage
{
  // NEVER DELETE MESSAGE TYPES
  enum Type
    {
      Heartbeat,
      Status,
      Decode,
      Clear,
      Reply,
      QSOLogged,
      Close,
      Replay,
      HaltTx,
      FreeText,
      WSPRDecode,
      Location,
      LoggedADIF,
      HighlightCallsign,
      SwitchConfiguration,
      Configure,
      maximum_message_type_     // ONLY add new message types
                                // immediately before here
    };

  quint32 constexpr pulse {15}; // seconds

  //
  // NetworkMessage::Builder - build a message containing serialized Qt types
  //
  class Builder
    : public QDataStream
  {
  public:
    static quint32 constexpr magic {0xadbccbda}; // never change this

    // increment this if a newer Qt schema is required and add decode
    // logic to the Builder and Reader class implementations
#if QT_VERSION >= 0x050400
    static quint32 constexpr schema_number {3};
#elif QT_VERSION >= 0x050200
    static quint32 constexpr schema_number {2};
#else
    // Schema 1 (Qt_5_0) is broken
#error "Qt version 5.2 or greater required"
#endif

    explicit Builder (QIODevice *, Type, QString const& id, quint32 schema);
    explicit Builder (QByteArray *, Type, QString const& id, quint32 schema);
    Builder (Builder const&) = delete;
    Builder& operator = (Builder const&) = delete;

  private:
    void common_initialization (Type type, QString const& id, quint32 schema);
  };

  //
  // NetworkMessage::Reader - read a message containing serialized Qt types
  //
  // Message  is as  per NetworkMessage::Builder  above, the  schema()
  // member  may be  used  to  determine the  schema  of the  original
  // message.
  //
  class Reader
    : public QDataStream
  {
  public:
    explicit Reader (QIODevice *);
    explicit Reader (QByteArray const&);
    Reader (Reader const&) = delete;
    Reader& operator = (Reader const&) = delete;
    ~Reader ();

    quint32 schema () const;
    Type type () const;
    QString id () const;

  private:
    class impl;
    pimpl<impl> m_;
  };
}

#endif
