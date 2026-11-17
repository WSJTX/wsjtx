#ifndef JTTY_RECEIVE_LINE_HPP
#define JTTY_RECEIVE_LINE_HPP

#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QPointer>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <memory>
#include <vector>

#include "widgets/JttyMessages.hpp"

class JttyReceiveLine
{
public:
  struct Presentation {
    quint64 group {};
    qint64 messageId {};
    QDateTime startUtc;
    double startSeconds {};
    int frequency {};
    QString message;
    int snr {-10};
    bool blankSnrColumn {false};  // a sent message has no measured SNR
    QColor background {};         // invalid QColor() means "no override"
    QColor foreground {};
  };

  struct Options {
    bool lowerCase {};
  };

  // Removed history is never recreated by a delayed update of the same message.
  QTextCursor render (QTextDocument& document, Presentation const& presentation,
                      Options const& options, QFont const& font,
                      bool chronological = false)
  {
    if (pruned_) return {};
    auto record = record_.lock ();
    if (rendered_) {
      if (!record || record->document != &document
          || record->presentation.group != presentation.group
          || record->presentation.messageId != presentation.messageId
          || !intact (record)) {
        pruned_ = true;
        record_.reset ();
        return {};
      }
    } else {
      QTextBlock before;
      QTextBlock after;
      if (chronological) {
        for (auto block = document.begin (); block.isValid (); block = block.next ()) {
          auto const * tag = dynamic_cast<Tag const *> (block.userData ());
          if (!tag || tag->index || tag->record->presentation.group != presentation.group
              || !intact (tag->record)) continue;
          auto const& known = tag->record->presentation;
          if (presentation.startSeconds < known.startSeconds
              || (presentation.startSeconds == known.startSeconds
                  && presentation.messageId < known.messageId)) {
            before = block;
            break;
          }
          after = tag->record->last;
        }
      }
      if (!before.isValid () && after.isValid ()) before = after.next ();
      if (before == document.begin () && document.maximumBlockCount () > 0
          && document.blockCount () >= document.maximumBlockCount ()) {
        pruned_ = true;
        return {};
      }
      auto cursor = insertBlock (document, before);
      record = std::make_shared<Record> ();
      record->document = &document;
      record->first = record->last = cursor.block ();
      record->first.setUserData (new Tag (record, 0));
      record_ = record;
      rendered_ = true;
    }
    record->presentation = presentation;
    return rewrite (record, options, font);
  }

  static void refresh (QTextDocument& document, Options const& options,
                       QFont const& font)
  {
    std::vector<std::shared_ptr<Record>> records;
    for (auto block = document.begin (); block.isValid (); block = block.next ()) {
      auto const * tag = dynamic_cast<Tag const *> (block.userData ());
      if (tag && !tag->index) records.push_back (tag->record);
    }
    for (auto const& record : records) {
      if (intact (record)) rewrite (record, options, font);
    }
  }

private:
  struct Record {
    QPointer<QTextDocument> document;
    Presentation presentation;
    QTextBlock first, last;
    QString text;
    QFont font;
    int blockCount {1};
  };

  struct Tag : QTextBlockUserData {
    Tag (std::shared_ptr<Record> const& source, int blockIndex)
      : record (source), index (blockIndex) {}
    std::shared_ptr<Record> record;
    int index;
  };

  static bool intact (std::shared_ptr<Record> const& record)
  {
    if (!record->document || !record->first.isValid () || !record->last.isValid ()) return false;
    int index = 0;
    for (auto block = record->first; block.isValid (); block = block.next (), ++index) {
      auto const * tag = dynamic_cast<Tag const *> (block.userData ());
      if (!tag || tag->record != record || tag->index != index) return false;
      if (block == record->last) {
        if (index + 1 != record->blockCount) return false;
        auto cursor = range (record);
        auto text = cursor.selectedText ();
        text.replace (QChar::ParagraphSeparator, QChar ('\n'));
        return text == record->text;
      }
    }
    return false;
  }

  static QTextCursor range (std::shared_ptr<Record> const& record)
  {
    QTextCursor cursor (record->first);
    QTextCursor end (record->last);
    end.movePosition (QTextCursor::EndOfBlock);
    cursor.setPosition (end.position (), QTextCursor::KeepAnchor);
    return cursor;
  }

  static QTextCursor insertBlock (QTextDocument& document, QTextBlock const& before)
  {
    QTextCursor cursor (&document);
    if (before.isValid ()) {
      auto previous = before.previous ();
      if (previous.isValid ()) {
        cursor = QTextCursor (previous);
        cursor.movePosition (QTextCursor::EndOfBlock);
        cursor.insertBlock (QTextBlockFormat {});
      } else {
        auto const * tag = dynamic_cast<Tag const *> (before.userData ());
        auto record = tag ? tag->record : std::shared_ptr<Record> {};
        int const index = tag ? tag->index : 0;
        auto const blockFormat = before.blockFormat ();
        auto const charFormat = before.charFormat ();
        cursor.insertBlock (blockFormat, charFormat);
        // Inserting at the first block moves its text into the new block.
        if (record) {
          auto moved = cursor.block ();
          moved.setUserData (new Tag (record, index));
          if (record->first == before) record->first = moved;
          if (record->last == before) record->last = moved;
        }
        cursor.movePosition (QTextCursor::PreviousBlock);
        cursor.setBlockFormat (QTextBlockFormat {});
      }
    } else {
      cursor.movePosition (QTextCursor::End);
      if (cursor.position ()) cursor.insertBlock (QTextBlockFormat {});
    }
    return cursor;
  }

  static QString formatted (Presentation const& presentation, Options const& options)
  {
    QString const message = options.lowerCase
      ? presentation.message.toLower () : presentation.message;
    QString text = presentation.blankSnrColumn
      ? Jtty::formatJttyTxLine (presentation.frequency, Jtty::wrapMessage (message))
      : Jtty::formatJttyDecodeLine (presentation.frequency, presentation.snr, Jtty::wrapMessage (message));
    auto const time = Jtty::jttyLineTimeLabel (presentation.startUtc);
    if (!time.isEmpty ()) text.prepend (time + " ");
    return text;
  }

  static QTextCursor rewrite (std::shared_ptr<Record> const& record,
                             Options const& options, QFont const& font)
  {
    QString const text = formatted (record->presentation, options);
    if (record->text == text && record->font == font) return {};
    auto cursor = range (record);
    cursor.removeSelectedText ();
    record->first = cursor.block ();
    record->first.setUserData (new Tag (record, 0));
    QTextCharFormat format;
    format.setFont (font);
    if (record->presentation.background.isValid ()) format.setBackground (record->presentation.background);
    if (record->presentation.foreground.isValid ()) format.setForeground (record->presentation.foreground);
    cursor.insertText (text, format);
    auto const * firstTag = record->first.isValid ()
      ? dynamic_cast<Tag const *> (record->first.userData ()) : nullptr;
    if (!firstTag || firstTag->record != record || firstTag->index) return {};
    record->last = cursor.block ();
    int index = 0;
    for (auto block = record->first; block.isValid (); block = block.next (), ++index) {
      block.setUserData (new Tag (record, index));
      if (block == record->last) break;
    }
    record->blockCount = index + 1;
    record->text = text;
    record->font = font;
    return cursor;
  }

  std::weak_ptr<Record> record_;
  bool rendered_ = false;
  bool pruned_ = false;
};

#endif
