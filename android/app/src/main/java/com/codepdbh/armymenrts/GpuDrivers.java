package com.codepdbh.armymenrts;

import android.content.Context;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;
import java.io.BufferedInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import org.json.JSONObject;

/**
 * Imported Vulkan drivers (Turnip for Adreno, Mali or Xclipse builds). They live in
 * app-private storage, as the linker refuses libraries from shared storage, and
 * adrenotools loads the selected one in place of the system driver.
 * Accepted: the usual driver .zip (meta.json + .so) or a bare .so file.
 */
final class GpuDrivers {
    static {System.loadLibrary("armymen_rts_android");}
    private static native void nativeSelect(String folder,String library,String hooks,String cache);
    static final String SYSTEM="";
    private static final long MAX_BYTES=256L<<20;

    static final class Driver {
        final String id,name,description,library;final File folder;
        Driver(String id,String name,String description,String library,File folder){
            this.id=id;this.name=name;this.description=description;this.library=library;this.folder=folder;
        }
    }

    private static File root(Context context){return new File(context.getFilesDir(),"gpu_drivers");}
    private static SharedPreferences preferences(Context context){return context.getSharedPreferences("gpu_drivers",Context.MODE_PRIVATE);}
    static String selected(Context context){return preferences(context).getString("selected",SYSTEM);}
    static void select(Context context,String id){preferences(context).edit().putString("selected",id).commit();}

    static List<Driver> list(Context context){
        List<Driver> drivers=new ArrayList<>();File[] folders=root(context).listFiles();
        if(folders==null)return drivers;
        for(File folder:folders){Driver driver=read(folder);if(driver!=null)drivers.add(driver);}
        drivers.sort((a,b)->a.name.compareToIgnoreCase(b.name));
        return drivers;
    }
    static Driver find(Context context,String id){
        if(id==null||id.isEmpty())return null;
        return read(new File(root(context),id));
    }
    private static Driver read(File folder){
        File meta=new File(folder,"meta.json");
        try{
            JSONObject json=new JSONObject(new String(readAll(new FileInputStream(meta),1<<20),StandardCharsets.UTF_8));
            String library=json.optString("libraryName","");
            if(library.isEmpty()||!new File(folder,library).isFile())return null;
            String description=json.optString("description","");
            String version=json.optString("driverVersion","");if(!version.isEmpty())description=(description.isEmpty()?"":description+" · ")+version;
            return new Driver(folder.getName(),json.optString("name",library),description,library,folder);
        }catch(Exception error){return null;}
    }

    /** Hands the selected driver (or the system one) to the native renderer. */
    static void apply(Context context){
        Driver driver=find(context,selected(context));
        String hooks=context.getApplicationInfo().nativeLibraryDir;String cache=context.getCacheDir().getAbsolutePath();
        if(driver==null)nativeSelect("","",hooks,cache);
        else nativeSelect(driver.folder.getAbsolutePath(),driver.library,hooks,cache);
    }

    static void delete(Context context,String id){
        File folder=new File(root(context),id);File[] files=folder.listFiles();
        if(files!=null)for(File file:files)file.delete();
        folder.delete();
        if(id.equals(selected(context)))select(context,SYSTEM);
    }

    /** Imports a driver package; returns it or throws with a message for the player. */
    static Driver importFrom(Context context,Uri uri)throws IOException{
        String display=displayName(context,uri);
        byte[] data;try(InputStream input=context.getContentResolver().openInputStream(uri)){
            if(input==null)throw new IOException("No se pudo abrir el archivo");data=readAll(input,MAX_BYTES);
        }
        File folder=new File(root(context),"driver_"+System.currentTimeMillis());
        if(!folder.mkdirs())throw new IOException("No se pudo crear la carpeta del driver");
        try{
            JSONObject meta=null;String library=null;
            if(data.length>4&&data[0]=='P'&&data[1]=='K'){
                try(ZipInputStream zip=new ZipInputStream(new java.io.ByteArrayInputStream(data))){
                    for(ZipEntry entry;(entry=zip.getNextEntry())!=null;){
                        if(entry.isDirectory())continue;
                        // Flatten: drivers are a few files; never trust paths from the archive.
                        String name=new File(entry.getName()).getName();
                        if(name.isEmpty()||name.startsWith("."))continue;
                        byte[] content=readAll(zip,MAX_BYTES);
                        if(name.equals("meta.json"))meta=new JSONObject(new String(content,StandardCharsets.UTF_8));
                        else if(name.endsWith(".so")||name.contains(".so.")){write(new File(folder,name),content);if(library==null||name.startsWith("vulkan."))library=name;}
                    }
                }
                if(meta!=null&&!meta.optString("libraryName","").isEmpty())library=meta.optString("libraryName");
            }else if(data.length>4&&data[0]==0x7f&&data[1]=='E'&&data[2]=='L'&&data[3]=='F'){
                library=display!=null&&display.endsWith(".so")?new File(display).getName():"vulkan.custom.so";
                write(new File(folder,library),data);
            }else throw new IOException("No es un driver: usa un .zip de driver (con meta.json) o un archivo .so");
            if(library==null||!new File(folder,library).isFile())throw new IOException("El paquete no contiene la librería del driver (.so)");
            if(meta==null)meta=new JSONObject();
            if(meta.optString("name","").isEmpty())meta.put("name",display!=null?display.replaceAll("\\.(zip|so)$",""):library);
            meta.put("libraryName",library);
            write(new File(folder,"meta.json"),meta.toString(2).getBytes(StandardCharsets.UTF_8));
            Driver driver=read(folder);if(driver==null)throw new IOException("Driver no válido");
            return driver;
        }catch(IOException error){delete(context,folder.getName());throw error;}
        catch(Exception error){delete(context,folder.getName());throw new IOException("Paquete de driver no válido: "+error.getMessage());}
    }

    private static String displayName(Context context,Uri uri){
        try(Cursor cursor=context.getContentResolver().query(uri,new String[]{OpenableColumns.DISPLAY_NAME},null,null,null)){
            if(cursor!=null&&cursor.moveToFirst())return cursor.getString(0);
        }catch(RuntimeException ignored){}
        return uri.getLastPathSegment();
    }
    private static byte[] readAll(InputStream input,long limit)throws IOException{
        InputStream source=input instanceof ZipInputStream?input:new BufferedInputStream(input);
        ByteArrayOutputStream output=new ByteArrayOutputStream();byte[] buffer=new byte[1<<16];long total=0;
        for(int read;(read=source.read(buffer))>0;){total+=read;if(total>limit)throw new IOException("Archivo demasiado grande");output.write(buffer,0,read);}
        if(!(input instanceof ZipInputStream))source.close();
        return output.toByteArray();
    }
    private static void write(File file,byte[] data)throws IOException{
        try(OutputStream output=new FileOutputStream(file)){output.write(data);}
        file.setReadable(true,true);file.setWritable(false,false);
    }
}
