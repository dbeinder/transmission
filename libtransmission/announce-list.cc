/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm>
#include <set>
#include <string>
#include <string_view>

#include "transmission.h"

#include "announce-list.h"
#include "torrent-metainfo.h"
#include "utils.h"
#include "variant.h"

size_t tr_announce_list::set(char const* const* announce_urls, tr_tracker_tier_t const* tiers, size_t n)
{
    trackers_.clear();

    for (size_t i = 0; i < n; ++i)
    {
        add(tiers[i], announce_urls[i]);
    }

    return size();
}

bool tr_announce_list::remove(std::string_view announce_url)
{
    auto it = find(announce_url);
    if (it == std::end(trackers_))
    {
        return false;
    }

    trackers_.erase(it);
    return true;
}

bool tr_announce_list::remove(tr_tracker_id_t id)
{
    auto it = find(id);
    if (it == std::end(trackers_))
    {
        return false;
    }

    trackers_.erase(it);
    return true;
}

bool tr_announce_list::replace(tr_tracker_id_t id, std::string_view announce_url_sv)
{
    auto const announce = tr_urlParseTracker(announce_url_sv);
    if (!announce || !canAdd(*announce))
    {
        return false;
    }

    auto it = find(id);
    if (it == std::end(trackers_))
    {
        return false;
    }

    auto const tier = it->tier;
    trackers_.erase(it);
    return add(tier, announce_url_sv);
}

bool tr_announce_list::add(tr_tracker_tier_t tier, std::string_view announce_url_sv)
{
    auto const announce = tr_urlParseTracker(announce_url_sv);
    if (!announce || !canAdd(*announce))
    {
        return false;
    }

    auto tracker = tracker_info{};
    tracker.announce_str = announce_url_sv;
    tracker.announce = *tr_urlParseTracker(tracker.announce_str.sv());
    tracker.tier = getTier(tier, *announce);
    tracker.id = nextUniqueId();
    auto host = std::string{ tracker.announce.host };
    host += ':';
    host += tracker.announce.portstr;
    tracker.host = host;

    auto const scrape_str = announceToScrape(announce_url_sv);
    if (scrape_str)
    {
        tracker.scrape_str = *scrape_str;
        tracker.scrape = *tr_urlParseTracker(tracker.scrape_str.sv());
    }

    auto const it = std::lower_bound(std::begin(trackers_), std::end(trackers_), tracker);
    trackers_.insert(it, tracker);
    return true;
}

std::optional<std::string> tr_announce_list::announceToScrape(std::string_view announce)
{
    // To derive the scrape URL use the following steps:
    // Begin with the announce URL. Find the last '/' in it.
    // If the text immediately following that '/' isn't 'announce'
    // it will be taken as a sign that that tracker doesn't support
    // the scrape convention. If it does, substitute 'scrape' for
    // 'announce' to find the scrape page.
    auto constexpr oldval = std::string_view{ "/announce" };
    if (auto pos = announce.rfind(oldval.front()); pos != std::string_view::npos && announce.find(oldval, pos) == pos)
    {
        auto const prefix = announce.substr(0, pos);
        auto const suffix = announce.substr(pos + std::size(oldval));
        return tr_strvJoin(prefix, std::string_view{ "/scrape" }, suffix);
    }

    // some torrents with UDP announce URLs don't have /announce
    if (tr_strvStartsWith(announce, std::string_view{ "udp:" }))
    {
        return std::string{ announce };
    }

    return {};
}

tr_quark tr_announce_list::announceToScrape(tr_quark announce)
{
    auto const scrape_str = announceToScrape(tr_quark_get_string_view(announce));
    if (scrape_str)
    {
        return tr_quark_new(*scrape_str);
    }
    return TR_KEY_NONE;
}

std::set<tr_tracker_tier_t> tr_announce_list::tiers() const
{
    auto tiers = std::set<tr_tracker_tier_t>{};
    for (auto const& tracker : trackers_)
    {
        tiers.insert(tracker.tier);
    }

    return tiers;
}

tr_tracker_tier_t tr_announce_list::nextTier() const
{
    return std::empty(trackers_) ? 0 : trackers_.back().tier + 1;
}

tr_tracker_id_t tr_announce_list::nextUniqueId()
{
    static tr_tracker_id_t id = 0;
    return id++;
}

tr_announce_list::trackers_t::iterator tr_announce_list::find(tr_tracker_id_t id)
{
    auto const test = [&id](auto const& tracker)
    {
        return tracker.id == id;
    };
    return std::find_if(std::begin(trackers_), std::end(trackers_), test);
}

tr_announce_list::trackers_t::iterator tr_announce_list::find(std::string_view announce)
{
    auto const test = [&announce](auto const& tracker)
    {
        return announce == tracker.announce.full;
    };
    return std::find_if(std::begin(trackers_), std::end(trackers_), test);
}

// if two announce URLs differ only by scheme, put them in the same tier.
// (note: this can leave gaps in the `tier' values, but since the calling
// function doesn't care, there's no point in removing the gaps...)
tr_tracker_tier_t tr_announce_list::getTier(tr_tracker_tier_t tier, tr_url_parsed_t const& announce) const
{
    auto const is_sibling = [&announce](tracker_info const& tracker)
    {
        return tracker.announce.host == announce.host && tracker.announce.path == announce.path;
    };

    auto const it = std::find_if(std::begin(trackers_), std::end(trackers_), is_sibling);
    return it != std::end(trackers_) ? it->tier : tier;
}

bool tr_announce_list::canAdd(tr_url_parsed_t const& announce)
{
    // looking at components instead of the full original URL lets
    // us weed out implicit-vs-explicit port duplicates e.g.
    // "http://tracker/announce" + "http://tracker:80/announce"
    auto const is_same = [&announce](auto const& tracker)
    {
        return tracker.announce.scheme == announce.scheme && tracker.announce.host == announce.host &&
            tracker.announce.port == announce.port && tracker.announce.path == announce.path;
    };
    return std::none_of(std::begin(trackers_), std::end(trackers_), is_same);
}

bool tr_announce_list::save(std::string_view torrent_file, tr_error** error) const
{
    // load the .torrent file
    auto metainfo = tr_variant{};
    if (!tr_variantFromFile(&metainfo, TR_VARIANT_PARSE_BENC, torrent_file, error))
    {
        return false;
    }

    // remove the old fields
    tr_variantDictRemove(&metainfo, TR_KEY_announce);
    tr_variantDictRemove(&metainfo, TR_KEY_announce_list);

    // add the new fields
    if (this->size() == 1)
    {
        tr_variantDictAddQuark(&metainfo, TR_KEY_announce, at(0).announce_str.quark());
    }
    else if (this->size() > 1)
    {
        tr_variant* tier_list = tr_variantDictAddList(&metainfo, TR_KEY_announce_list, 0);

        auto current_tier = std::optional<tr_tracker_tier_t>{};
        tr_variant* tracker_list = nullptr;

        for (auto const& tracker : *this)
        {
            if (tracker_list == nullptr || !current_tier || current_tier != tracker.tier)
            {
                tracker_list = tr_variantListAddList(tier_list, 1);
                current_tier = tracker.tier;
            }

            tr_variantListAddQuark(tracker_list, tracker.announce_str.quark());
        }
    }

    // convert it to benc
    auto benc_len = size_t{};
    auto* const benc = tr_variantToStr(&metainfo, TR_VARIANT_FMT_BENC, &benc_len);
    auto const benc_sv = std::string_view{ benc, benc_len };
    tr_variantFree(&metainfo);

    // confirm that it's good by parsing it back again
    auto tm = tr_torrent_metainfo{};
    if (!tm.parseBenc(benc_sv, error))
    {
        return false;
    }

    // save it
    auto const success = tr_saveFile(torrent_file, benc_sv, error);
    tr_free(benc);
    return success;
}
