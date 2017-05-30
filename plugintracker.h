/*
    This file is part of Kismet

    Kismet is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Kismet is distributed in the hope that it will be useful,
      but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Kismet; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef __PLUGINTRACKER_H__
#define __PLUGINTRACKER_H__

// Plugin handler
//
// Plugins are responsible for completing the record passed to them
// from Kismet and filling in the PluginRegistrationData record
// 
// Plugins must define two core functions, in the C name space:
//
// int kis_plugin_version_check(struct plugin_server_info *)
//
// will be passed an empty plugin_server_info struct and is expected
// to fill in all fields available.
//
// Plugins should return negative on failure, non-negative on success
//
// and
//
// int kis_plugin_register(GlobalRegistry *, PluginRegistrationData *)
//
// which is responsible for filling in the pluginregistration record
// and performing plugin initialization.
//
// Plugins should return negative on failure, non-negative on success
//
//
// Kismet plugins are first-order citizens in the ecosystem - a plugin
// is passed the global registry and is able to look up and interact
// with all registered components, including other plugins.
//
// This is a blessing and a curse - plugins are very tied to the kismet
// ABI, but are equally capable of performing ANYTHING kismet can
// do already.
//
// A secondary, abstracted plugin interface may come in the future to
// provide a more stable plugin interface.

#include "config.hpp"

#include <stdio.h>
#include <time.h>
#include <list>
#include <map>
#include <vector>
#include <algorithm>
#include <string>
#include <sys/types.h>
#include <dlfcn.h>
#include <dirent.h>

#include "globalregistry.h"

#include "trackedelement.h"
#include "kis_net_microhttpd.h"

// The registration object is created by the plugintracker and given to
// a Kismet plugin; the plugin fills in the relevant information during
// the registration process
class PluginRegistrationData : public tracker_component {
public:
    PluginRegistrationData(GlobalRegistry *in_globalreg, int in_id) :
        tracker_component(in_globalreg, in_id) {
        register_fields();
        reserve_fields(NULL);

        dlfile = NULL;
    }

    PluginRegistrationData(GlobalRegistry *in_globalreg, int in_id,
            SharedTrackerElement e) :
        tracker_component(in_globalreg, in_id) {

        register_fields();
        reserve_fields(e);

        dlfile = NULL;
    }

    virtual ~PluginRegistrationData() {
        if (dlfile != NULL)
            dlclose(dlfile);
    }

    virtual SharedTrackerElement clone_type() {
        return SharedTrackerElement(new PluginRegistrationData(globalreg, get_id()));
    }

    __Proxy(plugin_name, string, string, string, plugin_name);
    __Proxy(plugin_description, string, string, string, plugin_description);
    __Proxy(plugin_author, string, string, string, plugin_author);
    __Proxy(plugin_version, string, string, string, plugin_version);
    __Proxy(plugin_so, string, string, string, plugin_so);
    __Proxy(plugin_path, string, string, string, plugin_path);

    void set_plugin_dlfile(void *in_dlfile) {
        dlfile = in_dlfile;
    }

    void *get_plugin_dlfile() {
        return dlfile;
    }

protected:
    virtual void register_fields() {
        tracker_component::register_fields();

        RegisterField("kismet.plugin.name", TrackerString,
                "plugin name", &plugin_name);
        RegisterField("kismet.plugin.description", TrackerString,
                "plugin description", &plugin_description);
        RegisterField("kismet.plugin.author", TrackerString,
                "plugin author", &plugin_author);
        RegisterField("kismet.plugin.version", TrackerString,
                "plugin version", &plugin_version);
        RegisterField("kismet.plugin.shared_object", TrackerString,
                "plugin shared object filename", &plugin_so);
        RegisterField("kismet.plugin.path", TrackerString, 
                "complete path of plugin", &plugin_path);
    }

    SharedTrackerElement plugin_name;
    SharedTrackerElement plugin_author;
    SharedTrackerElement plugin_description;
    SharedTrackerElement plugin_version;

    SharedTrackerElement plugin_so;
    SharedTrackerElement plugin_path;

    void *dlfile;

};
typedef shared_ptr<PluginRegistrationData> SharedPluginData;

// Plugin information fetch function
typedef int (*plugin_register)(GlobalRegistry *, SharedPluginData);

#define KIS_PLUGINTRACKER_VERSION   1

// Server information record
// The plugin should fill in this data and return it in the kis_plugin_version_check
// callback.  It will be given a plugin_api_version which it must respect.
struct plugin_server_info {
    // V1 server info
    
    // Plugin API version; plugins can not expect fields to be present
    // in this struct from a future version of the plugin revision.  This
    // value is unlikely to change, but it may become necessary in the
    // future to expand the versioning
    unsigned int plugin_api_version;

    string kismet_major;
    string kismet_minor;
    string kismet_tiny;

    // End V1 info
};

// Version check callback def.  Plugin is called with a populated
// plugin_server_info record.
//
// Plugins are expected to return '1' if the version check is valid,
// and negative if the version check fails.
typedef int (*plugin_version_check)(plugin_server_info *);

// Plugin management class
class Plugintracker : public LifetimeGlobal,
    public Kis_Net_Httpd_CPPStream_Handler {
public:
    static shared_ptr<Plugintracker> create_plugintracker(GlobalRegistry *in_globalreg) {
        shared_ptr<Plugintracker> mon(new Plugintracker(in_globalreg));
        in_globalreg->RegisterLifetimeGlobal(mon);
        in_globalreg->InsertGlobal("PLUGINTRACKER", mon);
        return mon;
    }

private:
	Plugintracker(GlobalRegistry *in_globalreg);

public:
	static void Usage(char *name);

	virtual ~Plugintracker();

    // Look for plugins in the config file
    int ScanPlugins();

	// Activate the vector of plugins (called repeatedly during startup)
	int ActivatePlugins();

	// Shut down the plugins and close the shared files
	int ShutdownPlugins();

    // HTTP API
    virtual bool Httpd_VerifyPath(const char *path, const char *method);

    virtual void Httpd_CreateStreamResponse(Kis_Net_Httpd *httpd,
            Kis_Net_Httpd_Connection *connection,
            const char *url, const char *method, const char *upload_data,
            size_t *upload_data_size, std::stringstream &stream);

protected:
    pthread_mutex_t plugin_lock;

	GlobalRegistry *globalreg;
	int plugins_active;

	int ScanDirectory(DIR *in_dir, string in_path);

    // Final vector of registered activated plugins
    SharedTrackerElement plugin_registry;
    TrackerElementVector plugin_registry_vec;

    // List of plugins before they're loaded
    vector<SharedPluginData> plugin_preload;
};

#endif
