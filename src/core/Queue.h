#pragma once
// ---------------------------------------------------------------------------
// Shared play queue.
//
// One ordered list feeds both sources: YouTube videos (id) and local media files
// (path). The player only ever plays "the current item"; previous / next / skip /
// jump all operate on this model, so a YouTube playlist and a folder of mp4/mp3
// files behave identically in the interface.
// ---------------------------------------------------------------------------
#include <string>
#include <vector>

namespace lvs {

struct QueueItem {
  enum class Kind { YouTube, LocalFile };
  Kind kind = Kind::YouTube;
  std::string videoId; // YouTube only
  std::wstring path;   // LocalFile only
  std::string title;
  std::string duration; // "3:59" when known, empty otherwise
};

class Queue {
public:
  void clear() {
    items_.clear();
    current_ = -1;
    name_.clear();
  }

  bool empty() const { return items_.empty(); }
  int size() const { return (int)items_.size(); }
  int current() const { return current_; }
  const std::wstring& name() const { return name_; }
  void setName(std::wstring n) { name_ = std::move(n); }
  const std::vector<QueueItem>& items() const { return items_; }

  const QueueItem* at(int i) const {
    if (i < 0 || i >= (int)items_.size()) return nullptr;
    return &items_[(size_t)i];
  }

  const QueueItem* currentItem() const { return at(current_); }

  void setItems(std::vector<QueueItem> items, int startIndex) {
    items_ = std::move(items);
    current_ = items_.empty() ? -1 : (startIndex < 0 ? 0 : startIndex);
    if (current_ >= (int)items_.size()) current_ = (int)items_.size() - 1;
  }

  void append(QueueItem item) {
    items_.push_back(std::move(item));
    if (current_ < 0) current_ = 0;
  }

  // -1 when there is nothing after / before the current item.
  int nextIndex() const {
    const int n = current_ + 1;
    return (n >= 0 && n < (int)items_.size()) ? n : -1;
  }
  int prevIndex() const { return (current_ > 0 && current_ <= (int)items_.size()) ? current_ - 1 : -1; }

  bool jump(int i) {
    if (i < 0 || i >= (int)items_.size()) return false;
    current_ = i;
    return true;
  }
  bool advance() {
    const int n = nextIndex();
    if (n < 0) return false;
    current_ = n;
    return true;
  }
  bool back() {
    const int p = prevIndex();
    if (p < 0) return false;
    current_ = p;
    return true;
  }

private:
  std::vector<QueueItem> items_;
  int current_ = -1;
  std::wstring name_;
};

} // namespace lvs
