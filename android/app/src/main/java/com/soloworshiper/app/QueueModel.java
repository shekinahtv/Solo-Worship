package com.soloworshiper.app;

import java.util.ArrayList;
import java.util.List;

/**
 * One ordered play queue for both sources: YouTube videos (played by the official
 * embed) and local media files (played by the app's own player). Same model as the
 * Windows console, so previous / next / jump behave identically.
 */
public class QueueModel {

    public static class Item {
        public final boolean youtube;
        public final String id;      // YouTube video id, or the provider uri for files
        public final String title;
        public String duration = "";

        private Item(boolean youtube, String id, String title) {
            this.youtube = youtube;
            this.id = id;
            this.title = title;
        }

        public static Item youtube(String videoId, String title, String duration) {
            Item i = new Item(true, videoId, title);
            i.duration = duration == null ? "" : duration;
            return i;
        }

        public static Item file(String uri, String title) {
            return new Item(false, uri, title);
        }
    }

    private final List<Item> items = new ArrayList<>();
    private int current = -1;
    private String name = "";

    public void clear() {
        items.clear();
        current = -1;
        name = "";
    }

    public void setItems(List<Item> list, int startIndex) {
        items.clear();
        items.addAll(list);
        current = items.isEmpty() ? -1 : Math.max(0, Math.min(startIndex, items.size() - 1));
    }

    public void append(Item item) {
        if (item == null) return;
        items.add(item);
        if (current < 0) current = 0;
    }

    public void setName(String n) { name = n == null ? "" : n; }
    public String getName() { return name; }

    public boolean isEmpty() { return items.isEmpty(); }
    public int size() { return items.size(); }
    public int current() { return current; }
    public List<Item> items() { return items; }

    public Item at(int i) {
        return (i < 0 || i >= items.size()) ? null : items.get(i);
    }

    public Item currentItem() { return at(current); }

    public int nextIndex() {
        int n = current + 1;
        return (n >= 0 && n < items.size()) ? n : -1;
    }

    public int prevIndex() { return current > 0 ? current - 1 : -1; }

    public boolean jump(int i) {
        if (i < 0 || i >= items.size()) return false;
        current = i;
        return true;
    }

    public boolean advance() {
        int n = nextIndex();
        if (n < 0) return false;
        current = n;
        return true;
    }

    public boolean back() {
        int p = prevIndex();
        if (p < 0) return false;
        current = p;
        return true;
    }
}
